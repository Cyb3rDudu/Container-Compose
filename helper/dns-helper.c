// container-compose-dns-helper
//
// Privileged helper for container-compose. Adds or removes a single
// /etc/resolver/containerization.<domain> file (mirrors what `container
// system dns create|delete <domain>` does for the file-side state) and HUPs
// mDNSResponder so the new resolver is picked up.
//
// Privilege model
// ---------------
//   - Installed mode 4755, owned by root:wheel.
//   - Any user can invoke; binary runs as root via the setuid bit.
//   - Hardcodes the resolver directory and the `containerization.` filename
//     prefix. Domain comes from argv but is strictly validated to
//     [a-z0-9-]{1,63} with no leading/trailing hyphen — so the assembled
//     path cannot escape /etc/resolver/containerization.* .
//   - No shell, no system(), no popen(); the only exec'd binary is
//     /usr/bin/killall with hardcoded args and an empty environment.
//   - macOS strips DYLD_* env vars from setuid binaries automatically, so
//     library injection is not a concern. We do not read any environment.
//
// Usage:
//     container-compose-dns-helper add <domain>
//     container-compose-dns-helper rm <domain>
//
// Both ops are idempotent: `add` is a no-op success when the file already
// exists, `rm` is a no-op success when it doesn't.

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define RESOLVER_DIR     "/etc/resolver"
#define RESOLVER_PREFIX  "containerization."
#define MAX_DOMAIN_LEN   63

static int valid_domain(const char *s) {
    size_t n = strlen(s);
    if (n == 0 || n > MAX_DOMAIN_LEN) return 0;
    if (s[0] == '-' || s[n - 1] == '-') return 0;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        int ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
        if (!ok) return 0;
    }
    return 1;
}

static int hup_mdns(void) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        char *args[] = {"/usr/bin/killall", "-HUP", "mDNSResponder", NULL};
        char *env[]  = {NULL};
        execve("/usr/bin/killall", args, env);
        _exit(127);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return -1;
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) return 0;
    return -1;
}

static int do_add(const char *path, const char *domain) {
    struct stat st;
    if (stat(path, &st) == 0) return 0;  // already registered

    FILE *f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "container-compose-dns-helper: open %s for write: %s\n",
                path, strerror(errno));
        return 1;
    }
    int n = fprintf(f,
        "domain %s\n"
        "search %s\n"
        "nameserver 127.0.0.1\n"
        "port 2053\n",
        domain, domain);
    int closed = fclose(f);
    if (n < 0 || closed != 0) {
        fprintf(stderr, "container-compose-dns-helper: write %s: %s\n",
                path, strerror(errno));
        return 1;
    }
    if (chmod(path, 0644) != 0) {
        fprintf(stderr, "container-compose-dns-helper: chmod %s: %s\n",
                path, strerror(errno));
        return 1;
    }
    if (hup_mdns() != 0) {
        fprintf(stderr,
                "container-compose-dns-helper: file written but HUP mDNSResponder failed; "
                "run `sudo killall -HUP mDNSResponder` manually to activate\n");
        return 1;
    }
    return 0;
}

static int do_rm(const char *path) {
    if (unlink(path) != 0) {
        if (errno == ENOENT) return 0;  // already gone
        fprintf(stderr, "container-compose-dns-helper: unlink %s: %s\n",
                path, strerror(errno));
        return 1;
    }
    if (hup_mdns() != 0) {
        fprintf(stderr,
                "container-compose-dns-helper: file removed but HUP mDNSResponder failed; "
                "run `sudo killall -HUP mDNSResponder` manually to deactivate\n");
        return 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s {add|rm} <domain>\n",
                argc > 0 ? argv[0] : "container-compose-dns-helper");
        return 2;
    }
    const char *op     = argv[1];
    const char *domain = argv[2];

    if (!valid_domain(domain)) {
        fprintf(stderr,
                "container-compose-dns-helper: invalid domain '%s' "
                "(allowed: [a-z0-9-]{1,63}, no leading/trailing hyphen)\n",
                domain);
        return 2;
    }

    // Fail loudly when the binary isn't actually installed setuid-root —
    // otherwise the failure modes downstream are confusing.
    if (setuid(0) != 0) {
        fprintf(stderr,
                "container-compose-dns-helper: setuid(0) failed: %s "
                "(install with mode 4755 owned by root)\n",
                strerror(errno));
        return 1;
    }

    if (mkdir(RESOLVER_DIR, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "container-compose-dns-helper: mkdir %s: %s\n",
                RESOLVER_DIR, strerror(errno));
        return 1;
    }

    char path[256];
    int written = snprintf(path, sizeof(path), "%s/%s%s",
                           RESOLVER_DIR, RESOLVER_PREFIX, domain);
    if (written < 0 || (size_t)written >= sizeof(path)) {
        fprintf(stderr, "container-compose-dns-helper: resolver path too long\n");
        return 1;
    }

    if (strcmp(op, "add") == 0) return do_add(path, domain);
    if (strcmp(op, "rm")  == 0) return do_rm(path);

    fprintf(stderr,
            "container-compose-dns-helper: unknown op '%s' (use add or rm)\n", op);
    return 2;
}

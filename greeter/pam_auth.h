#ifndef CARDPUTER_ZERO_PAM_AUTH_H
#define CARDPUTER_ZERO_PAM_AUTH_H

#include <pwd.h>
#include <security/pam_appl.h>
#include <sys/types.h>

struct zero_pam_session {
    pam_handle_t *pamh;
    struct passwd pw;
    char username[256];
    int cred_established;
    int session_open;
};

int zero_pam_start_session(const char *username,
                           const char *password,
                           struct zero_pam_session *session);
void zero_pam_end_session(struct zero_pam_session *session, int pam_status);

#endif

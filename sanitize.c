// SPDX-License-Identifier: MIT

/* sanitize replaces everything in string "s" that is not [a-zA-Z0-9]
 * with an underscore. The resulting string is safe to pass to a shell.
 */
void sanitize(char* s)
{
    char c;
    for (int i = 0; s[i] != 0; i++) {
        c = s[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
            continue;
        }
        s[i] = '_';
    }
}

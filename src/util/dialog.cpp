/*
 *   Copyright (c) 2025-2026 Jawaid Bazyar

 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.

 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.

 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <SDL3/SDL.h>
#include <stdio.h>
#include "../gs2.hpp"

// Headless/CI safety: when running with -n (no_input / automation), a missing
// input file or any failure must FAIL to stderr with no GUI. A modal message
// box has no owner to dismiss it in a headless run and hangs the process
// forever. Gate every modal on no_input; always emit the message to stderr so
// CI/log capture sees it regardless of mode.
void system_failure(const char *message) {
    fprintf(stderr, "Error: %s\n", message);
    fflush(stderr);
    if (!gs2_app_values.no_input) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", message, NULL);
    }
}

void system_diag(char *message) {
    fprintf(stderr, "Information: %s\n", message);
    fflush(stderr);
    if (!gs2_app_values.no_input) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Information", message, NULL);
    }
}
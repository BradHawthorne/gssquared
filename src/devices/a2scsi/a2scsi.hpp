/*
 *   Copyright (c) 2025-2026 Jawaid Bazyar
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

// FORK: Apple II SCSI Card (Apple 670-0437, ROM 341-0437-A) with an NCR 5380
// and one direct-access SCSI target. See a2scsi.cpp for the sources it follows.

#pragma once

#include "gs2.hpp"
#include "computer.hpp"

void init_slot_a2scsi(computer_t *computer, SlotType_t slot);
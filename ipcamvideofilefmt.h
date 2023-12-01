// Copyright (C) 2023 Francesco Vannini
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
// Inspired by https://spitzner.org/kkmoon.html
//

#ifndef IPCAMVIDEOFILEFMT_H
#define IPCAMVIDEOFILEFMT_H

#define HXVS 1398167624
typedef struct HXVSFrame_t {
    uint32_t width;
    uint32_t height;
    uint8_t  padding[4];
} HXVSFrame_t;

#define HXVT 1414944840
typedef struct HXVTFrame_t {
    uint32_t width;
    uint32_t height;
    uint8_t  padding[4];
} HXVTFrame_t;

#define HXVF 1180063816
typedef struct HXVFFrame_t {
    uint32_t length;
    uint32_t timestamp;
    uint8_t  padding[4];
} HXVFFrame_t;

#define HXAF 1178687560
typedef struct HXAFFrame_t {
    uint32_t length;
    uint32_t timestamp;
    uint8_t  padding[8];
} HXAFFrame_t;

#define HXFI 1229346888
typedef struct HXFIFrame_t {
    uint32_t length;
    uint8_t  padding[12];
} HXFIFrame_t;

typedef struct HXFrame_t {
    uint32_t header;
    union {
        struct HXVSFrame_t hxvs;
        struct HXVTFrame_t hxvt;
        struct HXVFFrame_t hxvf;
        struct HXAFFrame_t hxaf;
        struct HXFIFrame_t hxfi;
    } data;
} HXFrame_t;

typedef struct H26X_Nal_Header_t { // https://stackoverflow.com/a/38095609
    uint8_t start_code[4];
    uint8_t unit_type:5;
    uint8_t nri:2;
    uint8_t f:1;
} H26X_Nal_Header_t;

#endif

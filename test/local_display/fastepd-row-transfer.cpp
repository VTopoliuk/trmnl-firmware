// FastEPD row transfer loop, pinned commit 9a84c35e52849f9f22d2a7ad5231ef501be49e04.
// Copyright (C) 2024-2026 BitBank Software, Inc.; Apache-2.0.
// Harness added for local live: no GPIO, no panel power, records DMA payloads only.
// See LICENSE-FastEPD. The loop below is unmodified upstream source;
// the regression test applies the same patch used by the actual firmware build.
#include <array>
#include <cstring>
#include <cstdio>
#include <vector>
#include <algorithm>
#include <cstdint>
struct FASTEPDSTATE {
    int native_width, native_height, iPartialPasses, iFlags;
    uint8_t *pTemp;
    uint8_t dma_buf[8];
};
constexpr int BB_PANEL_FLAG_MIRROR_Y = 4;
constexpr int ROW_START = 0;
std::vector<std::array<uint8_t, 4>> transfers;
void bbepRowControl(FASTEPDSTATE *, int) {}
void bbepWriteRow(FASTEPDSTATE *, uint8_t *data, int, bool) {
    std::array<uint8_t, 4> row;
    std::copy(data, data+4, row.begin());
    transfers.push_back(row);
}
void transferRows(FASTEPDSTATE *pState, int iStartLine, int iEndLine) {
    int i, pass, iDMAOff;
    for (pass = 0; pass < pState->iPartialPasses; pass++) { // each pass is about 32ms
        uint8_t *d, *dp = pState->pTemp;
        int iDelta = pState->native_width / 4; // 2 bits per pixel
        int iSkipped = 0;
        if (pState->iFlags & BB_PANEL_FLAG_MIRROR_Y) {
            dp = &pState->pTemp[(pState->native_height-1) * iDelta]; // read the memory upside down
            iDelta = -iDelta;
        }
        bbepRowControl(pState, ROW_START);
        iDMAOff = 0;
        for (i = 0; i < pState->native_height; i++) {
            d = &pState->dma_buf[iDMAOff];
            if (i >= iStartLine && i <= iEndLine) {
                // Send the data
                memcpy(d, dp, pState->native_width/4);
                bbepWriteRow(pState, d, (pState->native_width / 4), (i!=0));
                iSkipped = 0;
            } else {
               // write a neutral row
                if (iSkipped == 0) { // new skipped section
                    memset((void *)d, 0, pState->native_width/4);
                }
                bbepWriteRow(pState, d, (pState->native_width / 4), (i!=0));
                iSkipped++;
            }
            dp += iDelta;
            iDMAOff ^= (pState->native_width/4);
        }
    }
}
int main() {
    const int ranges[][2] = {{0,0},{0,3},{1,1},{2,4},{2,9},{9,11},{0,11}};
    for (int strength : {6, 8, 10}) {
    for (int flags : {0, BB_PANEL_FLAG_MIRROR_Y}) {
      for (int initial : {0, 0xa5}) {
        for (auto &range : ranges) {
          std::array<uint8_t, 48> waveform;
          for (int y=0; y<12; ++y) std::fill(waveform.begin()+y*4, waveform.begin()+(y+1)*4, y+1);
          FASTEPDSTATE state = {16, 12, strength, flags, waveform.data(), {}};
          std::fill(state.dma_buf, state.dma_buf+8, initial);
          transfers.clear();
          transferRows(&state, range[0], range[1]);
          if (transfers.size()!=size_t(strength * 12)) return 2;
          for (int pass=0; pass<strength; ++pass) for (int row=0; row<12; ++row) {
            bool dirty = row>=range[0] && row<=range[1];
            uint8_t expected = dirty ? (flags ? 12-row : row+1) : 0;
            for (uint8_t actual : transfers[pass*12+row]) if (actual!=expected) {
              std::printf("stale drive outside changed rows (range %d..%d, row %d)\n", range[0],range[1],row);
              return 1;
            }
          }
        }
      }
    }
    }
    std::puts("Both DMA buffers remain neutral outside every dirty range.");
}

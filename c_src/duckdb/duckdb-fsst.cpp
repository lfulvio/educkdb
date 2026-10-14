// See https://raw.githubusercontent.com/duckdb/duckdb/main/LICENSE for licensing information

#include "duckdb.hpp"
#include "duckdb-internal.hpp"
#ifndef DUCKDB_AMALGAMATION
#error header mismatch
#endif


// LICENSE_CHANGE_BEGIN
// The following code up to LICENSE_CHANGE_END is subject to THIRD PARTY LICENSE #5
// See the end of this file for a list

// this software is distributed under the MIT License (http://www.opensource.org/licenses/MIT):
//
// Copyright 2018-2020, CWI, TU Munich, FSU Jena
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files
// (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify,
// merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// - The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
// OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
// IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
// You can contact the authors via the FSST source repository : https://github.com/cwida/fsst


// LICENSE_CHANGE_BEGIN
// The following code up to LICENSE_CHANGE_END is subject to THIRD PARTY LICENSE #5
// See the end of this file for a list

// this software is distributed under the MIT License (http://www.opensource.org/licenses/MIT):
// 
// Copyright 2018-2020, CWI, TU Munich, FSU Jena
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files   
// (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify,   
// merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is   
// furnished to do so, subject to the following conditions:
// 
// - The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES 
// OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE 
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR 
// IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE. 
//                 
// You can contact the authors via the FSST source repository : https://github.com/cwida/fsst 
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <numeric>
#include <memory>
#include <queue>
#include <string>
#include <unordered_set>
#include <vector>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stddef.h>

using namespace std;

 // the official FSST API -- also usable by C mortals

/* unsigned integers */
namespace libfsst {
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
}  // namespace libfsst

#if UINTPTR_MAX == 0xffffffffU
// We're on a 32-bit platform
#define NONOPT_FSST
#endif

#define FSST_ENDIAN_MARKER ((u64) 1)
#define FSST_VERSION_20190218 20190218
#define FSST_VERSION ((u64) FSST_VERSION_20190218)

// "symbols" are character sequences (up to 8 bytes)
// A symbol is compressed into a "code" of, in principle, one byte. But, we added an exception mechanism:
// byte 255 followed by byte X represents the single-byte symbol X. Its code is 256+X.

// we represent codes in u16 (not u8). 12 bits code (of which 10 are used), 4 bits length
#define FSST_LEN_BITS       12
#define FSST_CODE_BITS      9 
#define FSST_CODE_BASE      256UL /* first 256 codes [0,255] are pseudo codes: escaped bytes */
#define FSST_CODE_MAX       (1UL<<FSST_CODE_BITS) /* all bits set: indicating a symbol that has not been assigned a code yet */
#define FSST_CODE_MASK      (FSST_CODE_MAX-1UL)   /* all bits set: indicating a symbol that has not been assigned a code yet */

namespace libfsst {
constexpr inline uint64_t swap64_if_be(uint64_t v) noexcept {
#if defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	#if defined(__clang__) || defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 3))
		return __builtin_bswap64(v);
	#elif
		return (v&0xff00000000000000ull) >> 56) | (v&0x00ff000000000000ull) >> 40 |  \
				(v&0x0000ff0000000000ull) >> 24 | (v&0x000000ff00000000ull) >> 8 |   \
				(v&0x00000000ff000000ull) << 8 | (v&0x0000000000ff0000ull) << 24 |   \
				(v&0x000000000000ff00ull) << 40 | (v&0x00000000000000ffull) << 56

	#endif
#else
    return v; // little-endian (or unknown), so no swap needed
#endif
}

inline uint64_t fsst_unaligned_load(u8 const* V) {
    uint64_t Ret;
    memcpy(&Ret, V, sizeof(uint64_t)); // compiler will generate efficient code (unaligned load, where possible)
    return swap64_if_be(Ret);
}

struct Symbol {
   static const unsigned maxLength = 8;

   // the byte sequence that this symbol stands for
   union { char str[maxLength]; u64 num; } val; // usually we process it as a num(ber), as this is fast

   // icl = u64 ignoredBits:16,code:12,length:4,unused:32 -- but we avoid exposing this bit-field notation
   u64 icl;  // use a single u64 to be sure "code" is accessed with one load and can be compared with one comparison

   Symbol() : icl(0) { val.num = 0; }

   explicit Symbol(u8 c, u16 code) : icl((1<<28)|(code<<16)|56) { store_num(c); } // single-char symbol
   explicit Symbol(const char* begin, const char* end) : Symbol(begin, (u32) (end-begin)) {}
   explicit Symbol(const u8* begin, const u8* end) : Symbol((const char*)begin, (u32) (end-begin)) {}
   explicit Symbol(const char* input, u32 len) {
      val.num = 0;
      if (len>=8) {
          len = 8;
          memcpy(val.str, input, 8);
      } else {
          memcpy(val.str, input, len);
      }
      set_code_len(FSST_CODE_MAX, len);
   }
   void set_code_len(u32 code, u32 len) { icl = (len<<28)|(code<<16)|((8-len)*8); }

   u64 load_num() const { return swap64_if_be(val.num); }
   void store_num(u64 v) { val.num = swap64_if_be(v); }

   u32 length() const { return (u32) (icl >> 28); }
   u16 code() const { return (icl >> 16) & FSST_CODE_MASK; }
   u32 ignoredBits() const { return (u32) icl; }

   u8 first() const { assert( length() >= 1); return 0xFF & load_num(); }
   u16 first2() const { assert( length() >= 2); return 0xFFFF & load_num(); }

#define FSST_HASH_LOG2SIZE 10 
#define FSST_HASH_PRIME 2971215073LL
#define FSST_SHIFT 15
#define FSST_HASH(w) (((w)*FSST_HASH_PRIME)^(((w)*FSST_HASH_PRIME)>>FSST_SHIFT))
   size_t hash() const { size_t v = 0xFFFFFF & load_num(); return FSST_HASH(v); } // hash on the next 3 bytes
};

// Symbol that can be put in a queue, ordered on gain
struct QSymbol{
   Symbol symbol;
   mutable u32 gain; // mutable because gain value should be ignored in find() on unordered_set of QSymbols
   bool operator==(const QSymbol& other) const { return symbol.val.num == other.symbol.val.num && symbol.length() == other.symbol.length(); }
};

// we construct FSST symbol tables using a random sample of about 16KB (1<<14) 
#define FSST_SAMPLETARGET (1<<14)
#define FSST_SAMPLEMAXSZ ((long) 2*FSST_SAMPLETARGET)

// two phases of compression, before and after optimize():
//
// (1) to encode values we probe (and maintain) three datastructures:
// - u16 byteCodes[256] array at the position of the next byte  (s.length==1)
// - u16 shortCodes[65536] array at the position of the next twobyte pattern (s.length==2)
// - Symbol hashtable[1024] (keyed by the next three bytes, ie for s.length>2), 
// this search will yield a u16 code, it points into Symbol symbols[]. You always find a hit, because the first 256 codes are 
// pseudo codes representing a single byte these will become escapes)
//
// (2) when we finished looking for the best symbol table we call optimize() to reshape it:
// - it renumbers the codes by length (first symbols of length 2,3,4,5,6,7,8; then 1 (starting from byteLim are symbols of length 1)
//   length 2 codes for which no longer suffix symbol exists (< suffixLim) come first among the 2-byte codes 
//   (allows shortcut during compression)
// - for each two-byte combination, in all unused slots of shortCodes[], it enters the byteCode[] of the symbol corresponding 
//   to the first byte (if such a single-byte symbol exists). This allows us to just probe the next two bytes (if there is only one
//   byte left in the string, there is still a terminator-byte added during compression) in shortCodes[]. That is, byteCodes[]
//   and its codepath is no longer required. This makes compression faster. The reason we use byteCodes[] during symbolTable construction
//   is that adding a new code/symbol is expensive (you have to touch shortCodes[] in 256 places). This optimization was
//   hence added to make symbolTable construction faster.
//
// this final layout allows for the fastest compression code, only currently present in compressBulk

// in the hash table, the icl field contains (low-to-high) ignoredBits:16,code:12,length:4
#define FSST_ICL_FREE ((15<<28)|(((u32)FSST_CODE_MASK)<<16)) // high bits of icl (len=8,code=FSST_CODE_MASK) indicates free bucket

// ignoredBits is (8-length)*8, which is the amount of high bits to zero in the input word before comparing with the hashtable key
//             ..it could of course be computed from len during lookup, but storing it precomputed in some loose bits is faster
//
// the gain field is only used in the symbol queue that sorts symbols on gain

struct SymbolTable {
   static const u32 hashTabSize = 1<<FSST_HASH_LOG2SIZE; // smallest size that incurs no precision loss

   // lookup table using the next two bytes (65536 codes), or just the next single byte
   u16 shortCodes[65536]; // contains code for 2-byte symbol, otherwise code for pseudo byte (escaped byte)

   // lookup table (only used during symbolTable construction, not during normal text compression)
   u16 byteCodes[256]; // contains code for every 1-byte symbol, otherwise code for pseudo byte (escaped byte)

   // 'symbols' is the current symbol  table symbol[code].symbol is the max 8-byte 'symbol' for single-byte 'code'
   Symbol symbols[FSST_CODE_MAX]; // x in [0,255]: pseudo symbols representing escaped byte x; x in [FSST_CODE_BASE=256,256+nSymbols]: real symbols   

   // replicate long symbols in hashTab (avoid indirection). 
   Symbol hashTab[hashTabSize]; // used for all symbols of 3 and more bytes

   u16 nSymbols;          // amount of symbols in the map (max 255)
   u16 suffixLim;         // codes higher than this do not have a longer suffix
   u16 terminator;        // code of 1-byte symbol, that can be used as a terminator during compression
   bool zeroTerminated;   // whether we are expecting zero-terminated strings (we then also produce zero-terminated compressed strings)
   u16 lenHisto[FSST_CODE_BITS]; // lenHisto[x] is the amount of symbols of byte-length (x+1) in this SymbolTable

   SymbolTable() : nSymbols(0), suffixLim(FSST_CODE_MAX), terminator(0), zeroTerminated(false) {
      // stuff done once at startup
      for (u32 i=0; i<256; i++) {
         symbols[i] = Symbol(i,i|(1<<FSST_LEN_BITS)); // pseudo symbols
      }
      Symbol unused = Symbol((u8) 0,FSST_CODE_MASK); // single-char symbol, exception code
      for (u32 i=256; i<FSST_CODE_MAX; i++) {
         symbols[i] = unused; // we start with all symbols unused
      }
      // empty hash table
      Symbol s;
      s.val.num = 0;
      s.icl = FSST_ICL_FREE; //marks empty in hashtab
      for(u32 i=0; i<hashTabSize; i++)
         hashTab[i] = s;

      // fill byteCodes[] with the pseudo code all bytes (escaped bytes)
      for(u32 i=0; i<256; i++)
         byteCodes[i] = (1<<FSST_LEN_BITS) | i;

      // fill shortCodes[] with the pseudo code for the first byte of each two-byte pattern
      for(u32 i=0; i<65536; i++)
         shortCodes[i] = (1<<FSST_LEN_BITS) | (i&255);

      memset(lenHisto, 0, sizeof(lenHisto)); // all unused
   }

   void clear() {
      // clear a symbolTable with minimal effort (only erase the used positions in it)
      memset(lenHisto, 0, sizeof(lenHisto)); // all unused
      for(u32 i=FSST_CODE_BASE; i<FSST_CODE_BASE+nSymbols; i++) {
          if (symbols[i].length() == 1) {
              u16 val = symbols[i].first();
              byteCodes[val] = (1<<FSST_LEN_BITS) | val;
          } else if (symbols[i].length() == 2) {
              u16 val = symbols[i].first2();
              shortCodes[val] = (1<<FSST_LEN_BITS) | (val&255);
          } else {
              u32 idx = symbols[i].hash() & (hashTabSize-1);
              hashTab[idx].val.num = 0;
              hashTab[idx].icl = FSST_ICL_FREE; //marks empty in hashtab
          }           
      } 
      nSymbols = 0; // no need to clean symbols[] as no symbols are used
   }
   bool hashInsert(Symbol s) {
      u32 idx = s.hash() & (hashTabSize-1);
      bool taken = (hashTab[idx].icl < FSST_ICL_FREE);
      if (taken) return false; // collision in hash table
      hashTab[idx].icl = s.icl;
      hashTab[idx].store_num(s.load_num() & (0xFFFFFFFFFFFFFFFF >> (u8) s.icl));
      return true;
   }
   bool add(Symbol s) {
      assert(FSST_CODE_BASE + nSymbols < FSST_CODE_MAX);
      u32 len = s.length();
      s.set_code_len(FSST_CODE_BASE + nSymbols, len);
      if (len == 1) {
         byteCodes[s.first()] = FSST_CODE_BASE + nSymbols + (1<<FSST_LEN_BITS); // len=1 (<<FSST_LEN_BITS)
      } else if (len == 2) {
         shortCodes[s.first2()] = FSST_CODE_BASE + nSymbols + (2<<FSST_LEN_BITS); // len=2 (<<FSST_LEN_BITS)
      } else if (!hashInsert(s)) {
         return false;
      }
      symbols[FSST_CODE_BASE + nSymbols++] = s;
      lenHisto[len-1]++;
      return true;
   }
   /// Find longest expansion, return code (= position in symbol table)
   u16 findLongestSymbol(Symbol s) const {
      size_t idx = s.hash() & (hashTabSize-1);
      if (hashTab[idx].icl <= s.icl && hashTab[idx].load_num() == (s.load_num() & (0xFFFFFFFFFFFFFFFF >> ((u8) hashTab[idx].icl)))) {
         return (hashTab[idx].icl>>16) & FSST_CODE_MASK; // matched a long symbol
      }
      if (s.length() >= 2) {
         u16 code =  shortCodes[s.first2()] & FSST_CODE_MASK;
         if (code >= FSST_CODE_BASE) return code; 
      }
      return byteCodes[s.first()] & FSST_CODE_MASK;
   }
   u16 findLongestSymbol(const u8* cur, const u8* end) const {
      return findLongestSymbol(Symbol(cur,end)); // represent the string as a temporary symbol
   }

   // rationale for finalize:
   // - during symbol table construction, we may create more than 256 codes, but bring it down to max 255 in the last makeTable()
   //   consequently we needed more than 8 bits during symbol table contruction, but can simplify the codes to single bytes in finalize()
   //   (this feature is in fact lo longer used, but could still be exploited: symbol construction creates no more than 255 symbols in each pass)
   // - we not only reduce the amount of codes to <255, but also *reorder* the symbols and renumber their codes, for higher compression perf.
   //   we renumber codes so they are grouped by length, to allow optimized scalar string compression (byteLim and suffixLim optimizations). 
   // - we make the use of byteCode[] no longer necessary by inserting single-byte codes in the free spots of shortCodes[]
   //   Using shortCodes[] only makes compression faster. When creating the symbolTable, however, using shortCodes[] for the single-byte
   //   symbols is slow, as each insert touches 256 positions in it. This optimization was added when optimizing symbolTable construction time.
   //
   // In all, we change the layout and coding, as follows..
   //
   // before finalize(): 
   // - The real symbols are symbols[256..256+nSymbols>. As we may have nSymbols > 255
   // - The first 256 codes are pseudo symbols (all escaped bytes)
   //
   // after finalize(): 
   // - table layout is symbols[0..nSymbols>, with nSymbols < 256. 
   // - Real codes are [0,nSymbols>. 8-th bit not set. 
   // - Escapes in shortCodes have the 8th bit set (value: 256+255=511). 255 because the code to be emitted is the escape byte 255
   // - symbols are grouped by length: 2,3,4,5,6,7,8, then 1 (single-byte codes last)
   // the two-byte codes are split in two sections: 
   // - first section contains codes for symbols for which there is no longer symbol (no suffix). It allows an early-out during compression
   //
   // finally, shortCodes[] is modified to also encode all single-byte symbols (hence byteCodes[] is not required on a critical path anymore).
   //
   void finalize(u8 zeroTerminated) {
       assert(nSymbols <= 255);
       u8 newCode[256], rsum[8], byteLim = nSymbols - (lenHisto[0] - zeroTerminated);

       // compute running sum of code lengths (starting offsets for each length) 
       rsum[0] = byteLim; // 1-byte codes are highest
       rsum[1] = zeroTerminated;
       for(u32 i=1; i<7; i++)
          rsum[i+1] = rsum[i] + lenHisto[i];

       // determine the new code for each symbol, ordered by length (and splitting 2byte symbols into two classes around suffixLim)
       suffixLim = rsum[1];
       symbols[newCode[0] = 0] = symbols[256]; // keep symbol 0 in place (for zeroTerminated cases only)

       for(u32 i=zeroTerminated, j=rsum[2]; i<nSymbols; i++) {  
          Symbol s1 = symbols[FSST_CODE_BASE+i];
          u32 len = s1.length(), opt = (len == 2)*nSymbols;
          if (opt) {
              u16 first2 = s1.first2();
              for(u32 k=0; k<opt; k++) {  
                 Symbol s2 = symbols[FSST_CODE_BASE+k];
                 if (k != i && s2.length() > 1 && first2 == s2.first2()) // test if symbol k is a suffix of s
                    opt = 0;
              }
              newCode[i] = opt?suffixLim++:--j; // symbols without a larger suffix have a code < suffixLim 
          } else 
              newCode[i] = rsum[len-1]++;
          s1.set_code_len(newCode[i],len);
          symbols[newCode[i]] = s1; 
       }
       // renumber the codes in byteCodes[] 
       for(u32 i=0; i<256; i++) 
          if ((byteCodes[i] & FSST_CODE_MASK) >= FSST_CODE_BASE)
             byteCodes[i] = newCode[(u8) byteCodes[i]] + (1 << FSST_LEN_BITS);
          else 
             byteCodes[i] = 511 + (1 << FSST_LEN_BITS);
       
       // renumber the codes in shortCodes[] 
       for(u32 i=0; i<65536; i++)
          if ((shortCodes[i] & FSST_CODE_MASK) >= FSST_CODE_BASE)
             shortCodes[i] = newCode[(u8) shortCodes[i]] + (shortCodes[i] & (15 << FSST_LEN_BITS));
          else 
             shortCodes[i] = byteCodes[i&0xFF];

       // replace the symbols in the hash table
       for(u32 i=0; i<hashTabSize; i++)
          if (hashTab[i].icl < FSST_ICL_FREE)
             hashTab[i] = symbols[newCode[(u8) hashTab[i].code()]];
   }
};

#ifdef NONOPT_FSST
struct Counters {
   u16 count1[FSST_CODE_MAX];   // array to count frequency of symbols as they occur in the sample 
   u16 count2[FSST_CODE_MAX][FSST_CODE_MAX]; // array to count subsequent combinations of two symbols in the sample 

   void count1Set(u32 pos1, u16 val) { 
      count1[pos1] = val;
   }
   void count1Inc(u32 pos1) { 
      count1[pos1]++;
   }
   void count2Inc(u32 pos1, u32 pos2) {  
      count2[pos1][pos2]++;
   }
   u32 count1GetNext(u32 &pos1) { 
      return count1[pos1];
   }
   u32 count2GetNext(u32 pos1, u32 &pos2) { 
      return count2[pos1][pos2];
   }
   void backup1(u8 *buf) {
      memcpy(buf, count1, FSST_CODE_MAX*sizeof(u16));
   }
   void restore1(u8 *buf) {
      memcpy(count1, buf, FSST_CODE_MAX*sizeof(u16));
   }
};
#else
// we keep two counters count1[pos] and count2[pos1][pos2] of resp 16 and 12-bits. Both are split into two columns for performance reasons
// first reason is to make the column we update the most during symbolTable construction (the low bits) thinner, thus reducing CPU cache pressure.
// second reason is that when scanning the array, after seeing a 64-bits 0 in the high bits column, we can quickly skip over many codes (15 or 7)
struct Counters {
   // high arrays come before low arrays, because our GetNext() methods may overrun their 64-bits reads a few bytes
   u8 count1High[FSST_CODE_MAX];   // array to count frequency of symbols as they occur in the sample (16-bits)
   u8 count1Low[FSST_CODE_MAX];    // it is split in a low and high byte: cnt = count1High*256 + count1Low
   u8 count2High[FSST_CODE_MAX][FSST_CODE_MAX/2]; // array to count subsequent combinations of two symbols in the sample (12-bits: 8-bits low, 4-bits high)
   u8 count2Low[FSST_CODE_MAX][FSST_CODE_MAX];    // its value is (count2High*256+count2Low) -- but high is 4-bits (we put two numbers in one, hence /2)
   // 385KB  -- but hot area likely just 10 + 30*4 = 130 cache lines (=8KB)
   
   void count1Set(u32 pos1, u16 val) { 
      count1Low[pos1] = val&255;
      count1High[pos1] = val>>8;
   }
   void count1Inc(u32 pos1) { 
      if (!count1Low[pos1]++) // increment high early (when low==0, not when low==255). This means (high > 0) <=> (cnt > 0)
         count1High[pos1]++; //(0,0)->(1,1)->..->(255,1)->(0,1)->(1,2)->(2,2)->(3,2)..(255,2)->(0,2)->(1,3)->(2,3)...
   }
   void count2Inc(u32 pos1, u32 pos2) {  
       if (!count2Low[pos1][pos2]++) // increment high early (when low==0, not when low==255). This means (high > 0) <=> (cnt > 0)
          // inc 4-bits high counter with 1<<0 (1) or 1<<4 (16) -- depending on whether pos2 is even or odd, repectively
          count2High[pos1][(pos2)>>1] += 1 << (((pos2)&1)<<2); // we take our chances with overflow.. (4K maxval, on a 8K sample)
   }
   u32 count1GetNext(u32 &pos1) { // note: we will advance pos1 to the next nonzero counter in register range
      // read 16-bits single symbol counter, split into two 8-bits numbers (count1Low, count1High), while skipping over zeros
      u64 high = fsst_unaligned_load(&count1High[pos1]); // note: this reads 8 subsequent counters [pos1..pos1+7]

      u32 zero = high?(__builtin_ctzll(high)>>3):7UL; // number of zero bytes
      high = (high >> (zero << 3)) & 255; // advance to nonzero counter
      if (((pos1 += zero) >= FSST_CODE_MAX) || !high) // SKIP! advance pos2
         return 0; // all zero

      u32 low = count1Low[pos1];
      if (low) high--; // high is incremented early and low late, so decrement high (unless low==0)
      return (u32) ((high << 8) + low);
   }
   u32 count2GetNext(u32 pos1, u32 &pos2) { // note: we will advance pos2 to the next nonzero counter in register range
      // read 12-bits pairwise symbol counter, split into low 8-bits and high 4-bits number while skipping over zeros
      u64 high = fsst_unaligned_load(&count2High[pos1][pos2>>1]); // note: this reads 16 subsequent counters [pos2..pos2+15]
      high >>= ((pos2&1) << 2); // odd pos2: ignore the lowest 4 bits & we see only 15 counters

      u32 zero = high?(__builtin_ctzll(high)>>2):(15UL-(pos2&1UL)); // number of zero 4-bits counters
      high = (high >> (zero << 2)) & 15;  // advance to nonzero counter
      if (((pos2 += zero) >= FSST_CODE_MAX) || !high) // SKIP! advance pos2
         return 0UL; // all zero

      u32 low = count2Low[pos1][pos2];
      if (low) high--; // high is incremented early and low late, so decrement high (unless low==0)
      return (u32) ((high << 8) + low);
   }
   void backup1(u8 *buf) {
      memcpy(buf, count1High, FSST_CODE_MAX);
      memcpy(buf+FSST_CODE_MAX, count1Low, FSST_CODE_MAX);
   }
   void restore1(u8 *buf) {
      memcpy(count1High, buf, FSST_CODE_MAX);
      memcpy(count1Low, buf+FSST_CODE_MAX, FSST_CODE_MAX);
   }
}; 
#endif


#define FSST_BUFSZ (3<<19) // 768KB

// an encoder is a symbolmap plus some bufferspace, needed during map construction as well as compression 
struct Encoder {
   shared_ptr<SymbolTable> symbolTable; // symbols, plus metadata and data structures for quick compression (shortCode,hashTab, etc)
   union {
      Counters counters;     // for counting symbol occurences during map construction
      u8 simdbuf[FSST_BUFSZ]; // for compression: SIMD string staging area 768KB = 256KB in + 512KB out (worst case for 256KB in) 
   };
};

// job control integer representable in one 64bits SIMD lane: cur/end=input, out=output, pos=which string (2^9=512 per call)
struct SIMDjob {
   u64 out:19,pos:9,end:18,cur:18; // cur/end is input offsets (2^18=256KB), out is output offset (2^19=512KB)  
};

// C++ fsst-compress function with some more control of how the compression happens (algorithm flavor, simd unroll degree)
size_t compressImpl(Encoder *encoder, size_t n, const size_t lenIn[], const u8 *strIn[], size_t size, u8 * output, size_t *lenOut, u8 *strOut[], bool noSuffixOpt, bool avoidBranch, int simd);
size_t compressAuto(Encoder *encoder, size_t n, const size_t lenIn[], const u8 *strIn[], size_t size, u8 * output, size_t *lenOut, u8 *strOut[], int simd);
}  // namespace libfsst


// LICENSE_CHANGE_END



namespace libfsst {
Symbol concat(Symbol a, Symbol b) {
	Symbol s;
	u32 length = a.length()+b.length();
	if (length > Symbol::maxLength) length = Symbol::maxLength;
	s.set_code_len(FSST_CODE_MASK, length);
	s.store_num((b.load_num() << (8*a.length())) | a.load_num());
	return s;
}
}  // namespace libfsst

namespace std {
template <>
class hash<libfsst::QSymbol> {
	public:
	size_t operator()(const libfsst::QSymbol& q) const {
		uint64_t k = q.symbol.load_num();
		const uint64_t m = 0xc6a4a7935bd1e995;
		const int r = 47;
		uint64_t h = 0x8445d61a4e774912 ^ (8*m);
		k *= m;
		k ^= k >> r;
		k *= m;
		h ^= k;
		h *= m;
		h ^= h >> r;
		h *= m;
		h ^= h >> r;
		return h;
	}
};
}

namespace libfsst {
bool isEscapeCode(u16 pos) { return pos < FSST_CODE_BASE; }

std::ostream& operator<<(std::ostream& out, const Symbol& s) {
	for (u32 i=0; i<s.length(); i++)
		out << s.val.str[i];
	return out;
}

SymbolTable *buildSymbolTable(Counters& counters, vector<const u8*> line, const size_t len[], bool zeroTerminated=false) {
	SymbolTable *st = new SymbolTable(), *bestTable = new SymbolTable();
	int bestGain = (int) -FSST_SAMPLEMAXSZ; // worst case (everything exception)
	size_t sampleFrac = 128;

	// start by determining the terminator. We use the (lowest) most infrequent byte as terminator
	st->zeroTerminated = zeroTerminated;
	if (zeroTerminated) {
		st->terminator = 0; // except in case of zeroTerminated mode, then byte 0 is terminator regardless frequency
	} else {
		u16 byteHisto[256];
		memset(byteHisto, 0, sizeof(byteHisto));
		for(size_t i=0; i<line.size(); i++) {
			const u8* cur = line[i];
			const u8* end = cur + len[i];
			while(cur < end) byteHisto[*cur++]++;
		}
		u32 minSize = FSST_SAMPLEMAXSZ, i = st->terminator = 256;
		while(i-- > 0) {
			if (byteHisto[i] > minSize) continue;
			st->terminator = i;
			minSize = byteHisto[i];
		}
	}
	assert(st->terminator != 256);

	// a random number between 0 and 128
	auto rnd128 = [&](size_t i) { return 1 + (FSST_HASH((i+1UL)*sampleFrac)&127); };

	// compress sample, and compute (pair-)frequencies
	auto compressCount = [&](SymbolTable *st, Counters &counters) { // returns gain
		int gain = 0;

		for(size_t i=0; i<line.size(); i++) {
			const u8* cur = line[i], *start = cur;
			const u8* end = cur + len[i];

			if (sampleFrac < 128) {
				// in earlier rounds (sampleFrac < 128) we skip data in the sample (reduces overall work ~2x)
				if (rnd128(i) > sampleFrac) continue;
			}
			if (cur < end) {
				u16 code2 = 255, code1 = st->findLongestSymbol(cur, end);
				cur += st->symbols[code1].length();
				gain += (int) (st->symbols[code1].length()-(1+isEscapeCode(code1)));
				while (true) {
					// count single symbol (i.e. an option is not extending it)
					counters.count1Inc(code1);

					// as an alternative, consider just using the next byte..
					if (st->symbols[code1].length() != 1) // .. but do not count single byte symbols doubly
						counters.count1Inc(*start);

					if (cur==end) {
						break;
					}

					// now match a new symbol
					start = cur;
					if (cur<end-7) {
						u64 word = fsst_unaligned_load(cur);
						size_t code = word & 0xFFFFFF;
						size_t idx = FSST_HASH(code)&(st->hashTabSize-1);
						Symbol s = st->hashTab[idx];
						code2 = st->shortCodes[word & 0xFFFF] & FSST_CODE_MASK;
						word &= (0xFFFFFFFFFFFFFFFF >> (u8) s.icl);
						if ((s.icl < FSST_ICL_FREE) & (s.load_num() == word)) {
							code2 = s.code();
							cur += s.length();
						} else if (code2 >= FSST_CODE_BASE) {
							cur += 2;
						} else {
							code2 = st->byteCodes[word & 0xFF] & FSST_CODE_MASK;
							cur += 1;
						}
					} else {
						code2 = st->findLongestSymbol(cur, end);
						cur += st->symbols[code2].length();
					}

					// compute compressed output size
					gain += ((int) (cur-start))-(1+isEscapeCode(code2));

					// now count the subsequent two symbols we encode as an extension codesibility
					if (sampleFrac < 128) { // no need to count pairs in final round
						                    // consider the symbol that is the concatenation of the two last symbols
						counters.count2Inc(code1, code2);

						// as an alternative, consider just extending with the next byte..
						if ((cur-start) > 1)  // ..but do not count single byte extensions doubly
							counters.count2Inc(code1, *start);
					}
					code1 = code2;
				}
			}
		}
		return gain;
	};

	auto makeTable = [&](SymbolTable *st, Counters &counters) {
		// hashmap of c (needed because we can generate duplicate candidates)
		unordered_set<QSymbol> cands;

		// artificially make terminater the most frequent symbol so it gets included
		u16 terminator = st->nSymbols?FSST_CODE_BASE:st->terminator;
		counters.count1Set(terminator,65535);

		auto addOrInc = [&](unordered_set<QSymbol> &cands, Symbol s, u64 count) {
			if (count < (5*sampleFrac)/128) return; // improves both compression speed (less candidates), but also quality!!
			QSymbol q;
			q.symbol = s;
			q.gain = count * s.length();
			auto it = cands.find(q);
			if (it != cands.end()) {
				q.gain += (*it).gain;
				cands.erase(*it);
			}
			cands.insert(q);
		};

		// add candidate symbols based on counted frequency
		for (u32 pos1=0; pos1<FSST_CODE_BASE+(size_t) st->nSymbols; pos1++) {
			u32 cnt1 = counters.count1GetNext(pos1); // may advance pos1!!
			if (!cnt1) continue;

			// heuristic: promoting single-byte symbols (*8) helps reduce exception rates and increases [de]compression speed
			Symbol s1 = st->symbols[pos1];
			addOrInc(cands, s1, ((s1.length()==1)?8LL:1LL)*cnt1);

			if (sampleFrac >= 128 || // last round we do not create new (combined) symbols
				s1.length() == Symbol::maxLength || // symbol cannot be extended
				s1.val.str[0] == st->terminator) { // multi-byte symbols cannot contain the terminator byte
				continue;
			}

			for (u32 pos2=0; pos2<FSST_CODE_BASE+(size_t)st->nSymbols; pos2++) {
				u32 cnt2 = counters.count2GetNext(pos1, pos2); // may advance pos2!!
				if (!cnt2) continue;

				// create a new symbol
				Symbol s2 = st->symbols[pos2];
				Symbol s3 = concat(s1, s2);
				if (s2.val.str[0] != st->terminator) // multi-byte symbols cannot contain the terminator byte
					addOrInc(cands, s3, cnt2);
			}
		}

		// insert candidates into priority queue (by gain)
		auto cmpGn = [](const QSymbol& q1, const QSymbol& q2) { return (q1.gain < q2.gain) || (q1.gain == q2.gain && q1.symbol.load_num() > q2.symbol.load_num()); };
		priority_queue<QSymbol,vector<QSymbol>,decltype(cmpGn)> pq(cmpGn);
		for (auto& q : cands)
			pq.push(q);

		// Create new symbol map using best candidates
		st->clear();
		while (st->nSymbols < 255 && !pq.empty()) {
			QSymbol q = pq.top();
			pq.pop();
			st->add(q.symbol);
		}
	};

	u8 bestCounters[512*sizeof(u16)];
#ifdef NONOPT_FSST
	for(size_t frac : {127, 127, 127, 127, 127, 127, 127, 127, 127, 128}) {
		sampleFrac = frac;
#else
	for(sampleFrac=8; true; sampleFrac += 30) {
#endif
		memset(&counters, 0, sizeof(Counters));
		long gain = compressCount(st, counters);
		if (gain >= bestGain) { // a new best solution!
			counters.backup1(bestCounters);
			*bestTable = *st; bestGain = gain;
		}
		if (sampleFrac >= 128) break; // we do 5 rounds (sampleFrac=8,38,68,98,128)
		makeTable(st, counters);
	}
	delete st;
	counters.restore1(bestCounters);
	makeTable(bestTable, counters);
	bestTable->finalize(zeroTerminated); // renumber codes for more efficient compression
	return bestTable;
}

// optimized adaptive *scalar* compression method
static inline size_t compressBulk(SymbolTable &symbolTable, size_t nlines, size_t lenIn[], u8* strIn[], size_t size, u8* out, size_t lenOut[], u8* strOut[], bool noSuffixOpt, bool avoidBranch) {
	const u8 *cur = NULL, *end =  NULL, *lim = out + size;
	size_t curLine, suffixLim = symbolTable.suffixLim;
	u8 byteLim = symbolTable.nSymbols + symbolTable.zeroTerminated - symbolTable.lenHisto[0];

	u8 buf[512+8] = {}; /* +8 sentinel is to avoid 8-byte unaligned-loads going beyond 511 out-of-bounds */

	// three variants are possible. dead code falls away since the bool arguments are constants
	auto compressVariant = [&](bool noSuffixOpt, bool avoidBranch) {
		while (cur < end) {
			u64 word = fsst_unaligned_load(cur);
			size_t code = symbolTable.shortCodes[word & 0xFFFF];
			if (noSuffixOpt && ((u8) code) < suffixLim) {
				// 2 byte code without having to worry about longer matches
				*out++ = (u8) code; cur += 2;
			} else {
				size_t pos = word & 0xFFFFFF;
				size_t idx = FSST_HASH(pos)&(symbolTable.hashTabSize-1);
				Symbol s = symbolTable.hashTab[idx];
				out[1] = (u8) word; // speculatively write out escaped byte
				word &= (0xFFFFFFFFFFFFFFFF >> (u8) s.icl);
				if ((s.icl < FSST_ICL_FREE) && s.load_num() == word) {
					*out++ = (u8) s.code(); cur += s.length();
				} else if (avoidBranch) {
					// could be a 2-byte or 1-byte code, or miss
					// handle everything with predication
					*out = (u8) code;
					out += 1+((code&FSST_CODE_BASE)>>8);
					cur += (code>>FSST_LEN_BITS);
				} else if ((u8) code < byteLim) {
					// 2 byte code after checking there is no longer pattern
					*out++ = (u8) code; cur += 2;
				} else {
					// 1 byte code or miss.
					*out = (u8) code;
					out += 1+((code&FSST_CODE_BASE)>>8); // predicated - tested with a branch, that was always worse
					cur++;
				}
			}
		}
	};

	for(curLine=0; curLine<nlines; curLine++) {
		size_t chunk, curOff = 0;
		strOut[curLine] = out;
		do {
			cur = strIn[curLine] + curOff;
			chunk = lenIn[curLine] - curOff;
			if (chunk > 511) {
				chunk = 511; // we need to compress in chunks of 511 in order to be byte-compatible with simd-compressed FSST
			}
			if ((2*chunk+7) > (size_t) (lim-out)) {
				return curLine; // out of memory
			}
			// copy the string to the 511-byte buffer
			memcpy(buf, cur, chunk);
			buf[chunk] = (u8) symbolTable.terminator;
			cur = buf;
			end = cur + chunk;

			// based on symboltable stats, choose a variant that is nice to the branch predictor
			if (noSuffixOpt) {
				compressVariant(true,false);
			} else if (avoidBranch) {
				compressVariant(false,true);
			} else {
				compressVariant(false, false);
			}
		} while((curOff += chunk) < lenIn[curLine]);
		lenOut[curLine] = (size_t) (out - strOut[curLine]);
	}
	return curLine;
}

#define FSST_SAMPLELINE ((size_t) 512)

// quickly select a uniformly random set of lines such that we have between [FSST_SAMPLETARGET,FSST_SAMPLEMAXSZ) string bytes
vector<const u8*> makeSample(u8* sampleBuf, u8* strIn[], size_t *lenIn, size_t nlines,
                                                    duckdb::unique_ptr<vector<size_t>>& sample_len_out) {
	size_t totSize = 0;
	vector<const u8*> sample;

	for(size_t i=0; i<nlines; i++)
		totSize += lenIn[i];

	if (totSize < FSST_SAMPLETARGET) {
		for(size_t i=0; i<nlines; i++)
			sample.push_back(strIn[i]);
	} else {
		size_t sampleRnd = FSST_HASH(4637947);
		const u8* sampleLim = sampleBuf + FSST_SAMPLETARGET;

		sample_len_out = duckdb::unique_ptr<vector<size_t>>(new vector<size_t>());
		sample_len_out->reserve(nlines + FSST_SAMPLEMAXSZ/FSST_SAMPLELINE);

		// This fails if we have a lot of small strings and a few big ones?
		while(sampleBuf < sampleLim) {
			// choose a non-empty line
			sampleRnd = FSST_HASH(sampleRnd);
			size_t linenr = sampleRnd % nlines;
			while (lenIn[linenr] == 0)
				if (++linenr == nlines) linenr = 0;

			// choose a chunk
			size_t chunks = 1 + ((lenIn[linenr]-1) / FSST_SAMPLELINE);
			sampleRnd = FSST_HASH(sampleRnd);
			size_t chunk = FSST_SAMPLELINE*(sampleRnd % chunks);

			// add the chunk to the sample
			size_t len = min(lenIn[linenr]-chunk,FSST_SAMPLELINE);
			memcpy(sampleBuf, strIn[linenr]+chunk, len);
			sample.push_back(sampleBuf);

			sample_len_out->push_back(len);
			sampleBuf += len;
		}
	}
	return sample;
}

extern "C" duckdb_fsst_encoder_t* duckdb_fsst_create(size_t n, size_t lenIn[], u8 *strIn[], int zeroTerminated) {
	u8* sampleBuf = new u8[FSST_SAMPLEMAXSZ];
	duckdb::unique_ptr<vector<size_t>> sample_sizes;
	vector<const u8*> sample = makeSample(sampleBuf, strIn, lenIn, n?n:1, sample_sizes); // careful handling of input to get a right-size and representative sample
	Encoder *encoder = new Encoder();
	const size_t* sampleLen = sample_sizes ? sample_sizes->data() : &lenIn[0];
	encoder->symbolTable = shared_ptr<SymbolTable>(buildSymbolTable(encoder->counters, sample, sampleLen, zeroTerminated));
	delete[] sampleBuf;
	return (duckdb_fsst_encoder_t*) encoder;
}

/* create another encoder instance, necessary to do multi-threaded encoding using the same symbol table */
extern "C" duckdb_fsst_encoder_t* duckdb_fsst_duplicate(duckdb_fsst_encoder_t *encoder) {
	Encoder *e = new Encoder();
	e->symbolTable = ((Encoder*)encoder)->symbolTable; // it is a shared_ptr
	return (duckdb_fsst_encoder_t*) e;
}

// export a symbol table in compact format.
extern "C" u32 duckdb_fsst_export(duckdb_fsst_encoder_t *encoder, u8 *buf) {
	Encoder *e = (Encoder*) encoder;
	// In ->version there is a versionnr, but we hide also suffixLim/terminator/nSymbols there.
	// This is sufficient in principle to *reconstruct* a duckdb_fsst_encoder_t from a duckdb_fsst_decoder_t
	// (such functionality could be useful to append compressed data to an existing block).
	//
	// However, the hash function in the encoder hash table is endian-sensitive, and given its
	// 'lossy perfect' hashing scheme is *unable* to contain other-endian-produced symbol tables.
	// Doing a endian-conversion during hashing will be slow and self-defeating.
	//
	// Overall, we could support reconstructing an encoder for incremental compression, but
	// should enforce equal-endianness. Bit of a bummer. Not going there now.
	//
	// The version field is now there just for future-proofness, but not used yet

	// version allows keeping track of fsst versions, track endianness, and encoder reconstruction
	u64 version = (FSST_VERSION << 32) |  // version is 24 bits, most significant byte is 0
	              (((u64) e->symbolTable->suffixLim) << 24) |
	              (((u64) e->symbolTable->terminator) << 16) |
	              (((u64) e->symbolTable->nSymbols) << 8) |
	              FSST_ENDIAN_MARKER; // least significant byte is nonzero

	version = swap64_if_be(version); // ensure version is little-endian encoded

	/* do not assume unaligned reads here */
	memcpy(buf, &version, 8);
	buf[8] = e->symbolTable->zeroTerminated;
	for(u32 i=0; i<8; i++)
		buf[9+i] = (u8) e->symbolTable->lenHisto[i];
	u32 pos = 17;

	// emit only the used bytes of the symbols
	for(u32 i = e->symbolTable->zeroTerminated; i < e->symbolTable->nSymbols; i++)
		for(u32 j = 0; j < e->symbolTable->symbols[i].length(); j++)
			buf[pos++] = e->symbolTable->symbols[i].val.str[j]; // serialize used symbol bytes

	return pos; // length of what was serialized
}

#define FSST_CORRUPT 32774747032022883 /* 7-byte number in little endian containing "corrupt" */

extern "C" u32 duckdb_fsst_import(duckdb_fsst_decoder_t *decoder, u8 *buf) {
	u64 version = 0;
	u32 code, pos = 17;
	u8 lenHisto[8];

	// version field (first 8 bytes) is now there just for future-proofness, unused still (skipped)
	memcpy(&version, buf, 8);
	version = swap64_if_be(version); // version is always little-endian encoded

	if ((version>>32) != FSST_VERSION) return 0;
	decoder->zeroTerminated = buf[8]&1;
	memcpy(lenHisto, buf+9, 8);

	// in case of zero-terminated, first symbol is "" (zero always, may be overwritten)
	decoder->len[0] = 1;
	decoder->symbol[0] = 0;

	// we use lenHisto[0] as 1-byte symbol run length (at the end)
	code = decoder->zeroTerminated;
	if (decoder->zeroTerminated) lenHisto[0]--; // if zeroTerminated, then symbol "" aka 1-byte code=0, is not stored at the end

	// now get all symbols from the buffer
	for(u32 l=1; l<=8; l++) { /* l = 1,2,3,4,5,6,7,8 */
		for(u32 i=0; i < lenHisto[(l&7) /* 1,2,3,4,5,6,7,0 */]; i++, code++)  {
			decoder->len[code] = (l&7)+1; /* len = 2,3,4,5,6,7,8,1  */
			decoder->symbol[code] = 0;
			for(u32 j=0; j<decoder->len[code]; j++)
				((u8*) &decoder->symbol[code])[j] = buf[pos++]; // note this enforces 'little endian' symbols
		}
	}
	if (decoder->zeroTerminated) lenHisto[0]++;

	// fill unused symbols with text "corrupt". Gives a chance to detect corrupted code sequences (if there are unused symbols).
	while(code<255) {
		decoder->symbol[code] = FSST_CORRUPT;
		decoder->len[code++] = 8;
	}
	return pos;
}

// runtime check for simd
inline size_t _compressImpl(Encoder *e, size_t nlines, size_t lenIn[], u8 *strIn[], size_t size, u8 *output, size_t *lenOut, u8 *strOut[], bool noSuffixOpt, bool avoidBranch, int) {
	return compressBulk(*e->symbolTable, nlines, lenIn, strIn, size, output, lenOut, strOut, noSuffixOpt, avoidBranch);
}
size_t compressImpl(Encoder *e, size_t nlines, size_t lenIn[], u8 *strIn[], size_t size, u8 *output, size_t *lenOut, u8 *strOut[], bool noSuffixOpt, bool avoidBranch, int simd) {
	return _compressImpl(e, nlines, lenIn, strIn, size, output, lenOut, strOut, noSuffixOpt, avoidBranch, simd);
}

// adaptive choosing of scalar compression method based on symbol length histogram
inline size_t _compressAuto(Encoder *e, size_t nlines, size_t lenIn[], u8 *strIn[], size_t size, u8 *output, size_t *lenOut, u8 *strOut[], int simd) {
	bool avoidBranch = false, noSuffixOpt = false;
	if (100*e->symbolTable->lenHisto[1] > 65*e->symbolTable->nSymbols && 100*e->symbolTable->suffixLim > 95*e->symbolTable->lenHisto[1]) {
		noSuffixOpt = true;
	} else if ((e->symbolTable->lenHisto[0] > 24 && e->symbolTable->lenHisto[0] < 92) &&
	           (e->symbolTable->lenHisto[0] < 43 || e->symbolTable->lenHisto[6] + e->symbolTable->lenHisto[7] < 29) &&
	           (e->symbolTable->lenHisto[0] < 72 || e->symbolTable->lenHisto[2] < 72)) {
		avoidBranch = true;
	}
	return _compressImpl(e, nlines, lenIn, strIn, size, output, lenOut, strOut, noSuffixOpt, avoidBranch, simd);
}
size_t compressAuto(Encoder *e, size_t nlines, size_t lenIn[], u8 *strIn[], size_t size, u8 *output, size_t *lenOut, u8 *strOut[], int simd) {
	return _compressAuto(e, nlines, lenIn, strIn, size, output, lenOut, strOut, simd);
}
}  // namespace libfsst

using namespace libfsst;
// the main compression function (everything automatic)
extern "C" size_t duckdb_fsst_compress(duckdb_fsst_encoder_t *encoder, size_t nlines, size_t lenIn[], u8 *strIn[], size_t size, u8 *output, size_t *lenOut, u8 *strOut[]) {
	// to be faster than scalar, simd needs 64 lines or more of length >=12; or fewer lines, but big ones (totLen > 32KB)
	size_t totLen = accumulate(lenIn, lenIn+nlines, 0);
	int simd = totLen > nlines*12 && (nlines > 64 || totLen > (size_t) 1<<15);
	return _compressAuto((Encoder*) encoder, nlines, lenIn, strIn, size, output, lenOut, strOut, 3*simd);
}

/* deallocate encoder */
extern "C" void duckdb_fsst_destroy(duckdb_fsst_encoder_t* encoder) {
	Encoder *e = (Encoder*) encoder;
	delete e;
}

/* very lazy implementation relying on export and import */
extern "C" duckdb_fsst_decoder_t duckdb_fsst_decoder(duckdb_fsst_encoder_t *encoder) {
	u8 buf[sizeof(duckdb_fsst_decoder_t)];
	u32 cnt1 = duckdb_fsst_export(encoder, buf);
	duckdb_fsst_decoder_t decoder;
	u32 cnt2 = duckdb_fsst_import(&decoder, buf);
	assert(cnt1 == cnt2); (void) cnt1; (void) cnt2;
	return decoder;
}


// LICENSE_CHANGE_END

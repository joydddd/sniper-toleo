#ifndef __SIFT_FORMAT_H
#define __SIFT_FORMAT_H

// Sniper Instruction Trace File Format
//
// x86-64 only, little-endian

namespace Sift
{

   const uint32_t MagicNumber = 0x54464953; // "SIFT"
   const uint32_t ICACHE_SIZE = 0x1000;
   const intptr_t ICACHE_OFFSET_MASK = ICACHE_SIZE - 1;
   const intptr_t ICACHE_PAGE_MASK = ~ICACHE_OFFSET_MASK;

   typedef struct
   {
      uint32_t magic;
      uint32_t size;             //< Size of extra header, in bytes
      uint64_t options;          //< Bit field of Option* options
      // Extra header options
      uint8_t  reserved[];
   } __attribute__ ((__packed__)) Header;

   typedef enum
   {
      CompressionZlib = 1,
      // Nothing yet
   } Option;

   typedef union
   {
      // Simple format for common instructions
      // * first 4 bits: non-zero
      // - address: previous address + previous size
      // - num_addresses: zero or one
      // - is_branch, taken
      // - predicate not supported

      struct {
         uint8_t size:4;    // 1-15
         uint8_t num_addresses:2;
         uint8_t is_branch:1;
         uint8_t taken:1;
         intptr_t addresses[];
      } __attribute__ ((__packed__)) Instruction;

      // Extended format for all instructions
      // * first 4 bits: zero, next 4 bits: non-zero

      struct {
         uint8_t type:4;   // 0
         uint8_t size:4;   // 1-15
         uint8_t num_addresses:2;
         uint8_t is_branch:1;
         uint8_t taken:1;
         uint8_t is_predicate:1;
         uint8_t executed:1;
         intptr_t addr;
         intptr_t addresses[];
      } __attribute__ ((__packed__)) InstructionExt;

      // Other stuff
      // * first 8 bits: zero

      struct {
         uint8_t zero;     // 0
         uint8_t type;
         uint32_t size;
         uint8_t data[];
      } __attribute__ ((__packed__)) Other;

   } __attribute__ ((__packed__)) Record;

   typedef enum {
      RecOtherIcache,
      RecOtherOutput,
      RecOtherEnd = 0xff,
   } RecOtherType;

   // Determine record type based on first uint8_t
   inline bool IsInstructionSimple(uint8_t byte) { return byte > 0; }

};

#endif // __SIFT_FORMAT_H
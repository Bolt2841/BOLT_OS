/* ===========================================================================
 * BOLT OS - Core Type Definitions
 * 
 * Re-exports types from lib/types.hpp for the storage subsystem.
 * =========================================================================== */

#ifndef BOLT_CORE_TYPES_HPP
#define BOLT_CORE_TYPES_HPP

#include "../lib/types.hpp"

// Make bolt types available at global scope for storage subsystem
using bolt::u8;
using bolt::u16;
using bolt::u32;
using bolt::u64;
using bolt::i8;
using bolt::i16;
using bolt::i32;
using bolt::i64;
using bolt::usize;

// Additional common types
typedef unsigned long ulong;
typedef unsigned int uint;

// Helper for unused parameters
#define UNUSED(x) (void)(x)

#endif // BOLT_CORE_TYPES_HPP

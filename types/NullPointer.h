///
/// @addtogroup LibDCPU-CI-Lang-C/Types
/// @{
///

///
/// @file       NullPointer.h
/// @brief      Declaration of the NullPointer type class.
/// @author     Patrick Flick
///

#include "Type.h"

#ifndef __DCPU_CC_TYPES_NULLPOINTER_H
#define __DCPU_CC_TYPES_NULLPOINTER_H

#include <string>
#include <stdint.h>

// include needed type classes
#include "IntegralType.h"


namespace dcpucc
{
    namespace types
    {

        ///
        /// @class      NullPointer
        /// @brief      The NullPointer type class.
        ///
        class NullPointer : public IntegralType
        {
            
        public:

            
        public:
            ///
            /// @brief      The constructor of the NullPointer type class.

            ///
            NullPointer()  {}
            
            ///
            /// @brief          Returns the size of the type in words.
            /// @return         The size of the type.
            ///
            /// This might return 0 if the type is not yet resolved
            /// (i.e. for structs).
            virtual uint16_t getWordSize();

            ///
            /// @brief          Returns the size of the type in bytes.
            /// @return         The size of the type.
            ///
            /// This might return 0 if the type is not yet resolved
            /// (i.e. for structs).
            virtual uint16_t getByteSize();
            
            ///
            /// @brief          Returns whether the type is a complete type.
            /// @return         True, if the type is a complete type, false
            ///                 otherwise
            ///
            virtual bool isComplete();
            
            
            ///
            /// @brief          The accept method of the visitor pattern.
            /// @param tv       The TypeVisitor to be accepted.
            /// @return         The boolean value returned by tv.accept(this)
            ///
            virtual bool accept(TypeVisitor& tv);
            
            ///
            /// @brief          Returns a string representation of the type.
            /// @return         A string representation of the type.
            ///
            /// This is used especially for error messages and warnings.
            virtual std::string toString();
            
            ///
            /// @brief      The destructor of the NullPointer type class.
            ///
            virtual ~NullPointer();
        };

    } // namespace types
} // namespace dcpucc

#endif

///
/// @}
///
///
/// @addtogroup LibDCPU-CI-Lang-C/CodeGen
/// @{
///

///
/// @file       BitField16.h
/// @brief      Declaration of the BitField16 class.
/// @author     Patrick Flick
///

#ifndef __DCPU_CC_CODEGEN_TYPEIMPL_BITFIELD16_H
#define __DCPU_CC_CODEGEN_TYPEIMPL_BITFIELD16_H

#include "TypeImplementation.h"
#include <deque>

namespace dcpucc
{
    namespace codegen
    {

        /// @brief      Implements bitfield operations
        class BitField16 : public TypeImplementation
        {
        private:
            int offset;
            int bitlen;
            
            void saveToStack(AsmBlock& ass, ValuePosition* vp);
            void loadFromStack(AsmBlock& ass, ValuePosition* vp);
            
        public:
            std::string mask_field;
            std::string mask_other;
            std::string shift_by;
            
            BitField16(int offset, int bitlen);
            
            virtual std::deque<std::string> printConstant(double value);
            virtual std::deque<std::string> printConstant(long value);
                        
            
            /// returns the size of the type
            virtual unsigned int getWordSize();
            
            /* increase/decrease ops */
            
            /// implements increase: B++
            virtual void inc(AsmBlock& ass, ValuePosition* posB, int by);
            
            /// implements decrease: B--
            virtual void dec(AsmBlock& ass, ValuePosition* posB, int by);
            
            
            /* unary operations */
            
            /// implements arithmetic inverse: B = -B
            virtual void ainv(AsmBlock& ass, ValuePosition* posB);
            
            /// implements binary inverse (not): B = ~B
            virtual void binv(AsmBlock& ass, ValuePosition* posB);
            
            /// implements logical inverse (not): C = ~B
            virtual void linv(AsmBlock& ass, ValuePosition* posC, ValuePosition* posB);
            
            
            /* arithmetic binary operations */
            
            /// implements addition: B = B + A
            virtual void add(AsmBlock& ass, ValuePosition* posB, ValuePosition* posA);
            
            /// implements subtraction: B = B - A
            virtual void sub(AsmBlock& ass, ValuePosition* posB, ValuePosition* posA);
            
            /// implements multiplication: B = B * A
            virtual void mul(AsmBlock& ass, ValuePosition* posB, ValuePosition* posA);
            
            /// implements division: B = B / A
            virtual void div(AsmBlock& ass, ValuePosition* posB, ValuePosition* posA);
            
            /// implements modulo: B = B % A
            virtual void mod(AsmBlock& ass, ValuePosition* posB, ValuePosition* posA);
            
            
            /* shift ops */
            
            /// implements left shift: B = B << A
            virtual void shl(AsmBlock& ass, ValuePosition* posB, ValuePosition* posA);
            
            /// implements right shift: B = B >> A
            virtual void shr(AsmBlock& ass, ValuePosition* posB, ValuePosition* posA);
            
            
            /* relational ops */
            
            /// implements equality check: C = (B == A)
            virtual void seq(AsmBlock& ass, ValuePosition* posC, ValuePosition* posB, ValuePosition* posA);
            
            /// implements not equal check: C = (B != A)
            virtual void sne(AsmBlock& ass, ValuePosition* posC, ValuePosition* posB, ValuePosition* posA);
            
            /// implements less to check: C = (B < A)
            virtual void slt(AsmBlock& ass, ValuePosition* posC, ValuePosition* posB, ValuePosition* posA);
            
            /// implements less or equal to check: C = (B <= A)
            virtual void sle(AsmBlock& ass, ValuePosition* posC, ValuePosition* posB, ValuePosition* posA);
            
            /// implements greater to check: C = (B > A)
            virtual void sgt(AsmBlock& ass, ValuePosition* posC, ValuePosition* posB, ValuePosition* posA);
            
            /// implements greater or equal to check: C = (B >= A)
            virtual void sge(AsmBlock& ass, ValuePosition* posC, ValuePosition* posB, ValuePosition* posA);
            
            
            /* binary ops */
            
            /// implements binary and: B = B & A
            virtual void band(AsmBlock& ass, ValuePosition* posB, ValuePosition* posA);
            
            /// implements binary or: B = B | A
            virtual void bor(AsmBlock& ass, ValuePosition* posB, ValuePosition* posA);
            
            /// implements binary xor: B = B ^ A
            virtual void bxor(AsmBlock& ass, ValuePosition* posB, ValuePosition* posA);
            
           
            
            /* logical ops */
            
            /// implements logical and: C = B && A
            virtual void land(AsmBlock& ass, ValuePosition* posC, ValuePosition* posB, ValuePosition* posA);
            
            /// implements logical or: C = B || A
            virtual void lor(AsmBlock& ass, ValuePosition* posC, ValuePosition* posB, ValuePosition* posA);
            
            
            /* conditional jump instructions */
            
            /// implements IF A == 0: JUMP label
            virtual void jmpeqz(AsmBlock& ass, ValuePosition* posA, std::string label);
            
            /// implements IF A != 0: JUMP label
            virtual void jmpneqz(AsmBlock& ass, ValuePosition* posA, std::string label);
            
            /// implements IF A == constant: JUMP label
            virtual void jmpneq(AsmBlock& ass, ValuePosition* posA, std::string label, long integralConst);
            
            /// implements IF A == constant: JUMP label
            virtual void jmpneq(AsmBlock& ass, ValuePosition* posA, std::string label, double floatConst);
        };


    } // namespace codegen
} // namespace dcpucc

#endif

///
/// @addtogroup LibDCPU-CI-Lang-C/AST-Nodes
/// @{
///

///
/// @file       Pointer.cpp
/// @brief      Definition of the Pointer AST node.
/// @author     Patrick Flick
///

#include "Pointer.h"

using namespace dtcc::astnodes;

// calls accept(visitor) for all children nodes of this AST node
void Pointer::allChildrenAccept(dtcc::visitor::Visitor & visitor)
{
    // TODO do we need to visit type qualifiers??
    //this->typeQualifiers->accept(visitor);

}

// implements the visitor pattern
void Pointer::accept(dtcc::visitor::Visitor & visitor)
{
    visitor.visit(this);
}

// implements the post recursive visitor pattern
void Pointer::acceptPostRecursive(dtcc::visitor::Visitor & visitor)
{
    visitor.visit(this);
    this->allChildrenAccept(visitor);
}

// implements the pre recursive visitor pattern
void Pointer::acceptPreRecursive(dtcc::visitor::Visitor & visitor)
{
    this->allChildrenAccept(visitor);
    visitor.visit(this);
}

// implements the destructor, which deletes all children
Pointer::~Pointer()
{
    delete this->typeQualifiers;

}

///
/// @}
///
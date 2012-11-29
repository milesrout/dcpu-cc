///
/// @addtogroup LibDCPU-CI-Lang-C/AST-Nodes
/// @{
///

///
/// @file       ChainExpressions.cpp
/// @brief      Definition of the ChainExpressions AST node.
/// @author     Patrick Flick
///

#include "ChainExpressions.h"

using namespace dtcc::astnodes;

// calls accept(visitor) for all children nodes of this AST node
void ChainExpressions::allChildrenAccept(dtcc::visitor::Visitor & visitor)
{
    // TODO implement this to call .accept(visitor) for all children nodes

}

// implements the visitor pattern
void ChainExpressions::accept(dtcc::visitor::Visitor & visitor)
{
    visitor.visit(this);
}

// implements the post recursive visitor pattern
void ChainExpressions::acceptPostRecursive(dtcc::visitor::Visitor & visitor)
{
    visitor.visit(this);
    this->allChildrenAccept(visitor);
}

// implements the pre recursive visitor pattern
void ChainExpressions::acceptPreRecursive(dtcc::visitor::Visitor & visitor)
{
    this->allChildrenAccept(visitor);
    visitor.visit(this);
}

// implements the destructor, which deletes all children
ChainExpressions::~ChainExpressions()
{
    delete this->exprs;
}

///
/// @}
///
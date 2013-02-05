#include "SemanticCheckVisitor.h"
#include <iostream>
#include <string>
#include <deque>
#include <utility>
#include <algorithm>

#include <errors/derr.defs.h>
#include <errors/InternalCompilerException.h>

#include <types/alltypes.h>
#include <types/IsTypeHelper.h>

#include <visitor/ConstExprEvalVisitor.h>

#include <valuetypes/ValueType.h>
#include <valuetypes/FunctionDesignator.h>
#include <valuetypes/LValue.h>
#include <valuetypes/RValue.h>
#include <valuetypes/CValue.h>
#include <valuetypes/IsValueTypeHelper.h>
#include <valuetypes/PromotionHelper.h>
#include <valuetypes/ConstHelper.h>

// only include the int tokens
#define YYSTYPE int
#include <parser.hpp>

using namespace dtcc;
using namespace dtcc::visitor;


/* constructor */

SemanticCheckVisitor::SemanticCheckVisitor()
{
    this->m_loopStack = std::deque<std::pair<astnodes::LabelStatement*, astnodes::LabelStatement*> >();
    this->m_symbolTable = new symboltable::SymbolTable();
    this->m_errorList = errors::ErrorList();
    this->m_AutomaticLabels = std::set<std::string>();
    this->m_switchStack = std::deque<astnodes::SwitchStatement*>();
    this->m_funcLabels = std::map<std::string, astnodes::LabelStatement*>();
    this->m_invalidValType = new valuetypes::RValue(new types::InvalidType());
}

// TODO this is just while dev, to be removed in final version:
void SemanticCheckVisitor::printAstName(const char * name)
{
    std::cout << "TODO implement semantic check for node '" << name << "'." << std::endl;
}

valuetypes::ValueType* SemanticCheckVisitor::getInvalidValType()
{
    return new valuetypes::RValue(new types::InvalidType());
}


/******************************/
/* break and continue         */
/* label management           */
/******************************/

void SemanticCheckVisitor::initLoopStack()
{
    this->m_loopStack.clear();
}
void SemanticCheckVisitor::popLoopStack()
{
    this->m_loopStack.pop_back();
}

void SemanticCheckVisitor::pushLoopStack(astnodes::LabelStatement* breakLabel, astnodes::LabelStatement* continueLabel)
{
    this->m_loopStack.push_back(std::pair<astnodes::LabelStatement*, astnodes::LabelStatement*>(breakLabel, continueLabel));
}

astnodes::LabelStatement* SemanticCheckVisitor::getBreakLabel()
{
    if (this->m_loopStack.size() > 0)
    {
        return this->m_loopStack.back().first;
    }
    else
    {
        return NULL;
    }
}

astnodes::LabelStatement* SemanticCheckVisitor::getContinueLabel()
{
    // this can be inside a switch (which just uses break)
    // so search in the list for the next non NULL continue label
    for (std::deque<std::pair<astnodes::LabelStatement*, astnodes::LabelStatement*> >::reverse_iterator it = this->m_loopStack.rbegin(); it != this->m_loopStack.rend(); it++)
        if (it->second != NULL)
            return it->second;
    return NULL;
}

// Generates a random, unique label for use in code.
astnodes::LabelStatement* SemanticCheckVisitor::getRandomLabel(std::string prefix)
{
    std::string result = "";
    
    while ((result == "") || (this->m_AutomaticLabels.find(result) != this->m_AutomaticLabels.end()))
        result = "__" + prefix + "_" + SemanticCheckVisitor::getRandomString(10);
    
    return new astnodes::LabelStatement(result, new astnodes::EmptyStatement());
}

// Generates a random character.
char SemanticCheckVisitor::getRandomCharacter()
{
    unsigned char c;
    
    while (!std::isalnum(c = static_cast<unsigned char>(std::rand() % 256))) ;
    
    return c;
}

// Generates a random string.
std::string SemanticCheckVisitor::getRandomString(std::string::size_type sz)
{
    std::string s;
    s.reserve(sz);
    std::generate_n(std::back_inserter(s), sz, SemanticCheckVisitor::getRandomCharacter);
    return s;
}



/******************************/
/* error & warning management */
/******************************/

void SemanticCheckVisitor::addError(astnodes::Node* node, int errid)
{
    this->m_errorList.addError(node->line, node->file, errid);
}

void SemanticCheckVisitor::addError(astnodes::Node* node, int errid, std::string msg)
{
    this->m_errorList.addError(node->line, node->file, errid, msg);
}

void SemanticCheckVisitor::addWarning(astnodes::Node* node, int errid)
{
    this->m_errorList.addWarning(node->line, node->file, errid);
}

void SemanticCheckVisitor::addWarning(astnodes::Node* node, int errid, std::string msg)
{
    this->m_errorList.addWarning(node->line, node->file, errid, msg);
}



/******************************/
/* visit functions            */
/******************************/


void SemanticCheckVisitor::visit(astnodes::Program * program)
{
    // set declarations to global
    this->m_declState = DECLSTATE_GLOBAL;
    
    // analyse everything
    program->allChildrenAccept(*this);
}


void SemanticCheckVisitor::visit(astnodes::FunctionDefinition * functionDefinition)
{
    // clear labels
    m_funcLabels.clear();
    
    // get type from declaration specifiers
    types::Type* returnType = declSpecsToType(functionDefinition->declSpecifiers);
    
    // check that the type is either void or an object type, but not an array
    if (types::IsTypeHelper::isArrayType(returnType) || (!types::IsTypeHelper::isObjectType(returnType) && ! types::IsTypeHelper::isVoid(returnType)))
    {
        addError(functionDefinition, ERR_CC_FUNC_RETURN_TYPE);
    }
    
    // next check for the storage specifiers
    if (functionDefinition->declSpecifiers->storageSpecifiers.size() > 1)
    {
        addError(functionDefinition->declSpecifiers->storageSpecifiers.front(), ERR_CC_STORSPEC_CONFL);
        return;
    } else if (functionDefinition->declSpecifiers->storageSpecifiers.size() == 1)
    {
        switch(functionDefinition->declSpecifiers->storageSpecifiers.front()->token)
        {
            case STATIC:
            case EXTERN:
                // TODO use it do decide on visibility
                break;
            case TYPEDEF:
            case AUTO:
            case REGISTER:
                addError(functionDefinition->declSpecifiers->storageSpecifiers.front(), ERR_CC_FUNC_RETURN_STORAGE);
                return;
                break;
            default:
                throw new errors::InternalCompilerException("unknown storage specifier encountered");
        }
    }
    
    
    symboltable::SymbolTableScope* funScope = this->m_symbolTable->beginNewScope();
    
    // get all the parameters with their names into the scope
    this->m_curDeclType = returnType;
    this->m_declState = DECLSTATE_FUNDEF;
    functionDefinition->declarator->accept(*this);
    // get the function type
    types::Type* funType = this->m_curDeclType;
    // get name
    std::string name = this->m_functionName;
    
    // check that it is actually a function type
    if (! types::IsTypeHelper::isFunctionType(funType))
    {
        addError(functionDefinition, ERR_CC_FUNC_IS_NOT_A_FUNC_TYPE);
        return;
    }
    
    // end the parameter scope before checking for the function in the symbol table
    this->m_symbolTable->endScope();
    

    
    symboltable::SymbolObject obj;
    // search for functions only at global scope:
    if(this->m_symbolTable->getGlobalScope().findSymbol(name, obj))
    {
        if (obj.declType == symboltable::FUNCTION_DEF)
        {
            addError(functionDefinition, ERR_CC_REDECL, name);
            return;
        }
        else if (obj.declType != symboltable::FUNCTION_DECL)
        {
            addError(functionDefinition, ERR_CC_REDECL, name);
            return;
        }
    }
    
    // insert function
    this->m_symbolTable->getGlobalScope().insertSymbol(name, symboltable::FUNCTION_DEF, funType, symboltable::GLOBAL);
    
    this->m_declState = DECLSTATE_LOCAL;
    
    // give the block its scope
    functionDefinition->block->scope = funScope;
    // analyze the declarations and statements in the block
    functionDefinition->block->accept(*this);
    
    // the scope is closed by the block itself
    // no need to do it here
    
    // set the name of the function
    functionDefinition->name = name;
    
    // set the parameter and locals size
    functionDefinition->stackSize = funScope->getLocalStackSize();
    functionDefinition->paramSize = funScope->getParameterStackSize();
    
    // TODO call goto label resolval visitor here:
    
    // clear labels
    m_funcLabels.clear();
    
    // set state back to global
    this->m_declState = DECLSTATE_GLOBAL;
}


void SemanticCheckVisitor::visit(astnodes::GotoStatement * gotoStatement)
{
    // goto statements are resolved in an extra pass
}


void SemanticCheckVisitor::visit(astnodes::ContinueStatement * continueStatement)
{
    astnodes::LabelStatement* label = getContinueLabel();
    if (label == NULL)
    {
        addError(continueStatement, ERR_CC_CONTINUE_OUTSIDE_OF_LOOP);
        return;
    }
    continueStatement->label = label;
}


void SemanticCheckVisitor::visit(astnodes::BreakStatement * breakStatement)
{
    astnodes::LabelStatement* label = getBreakLabel();
    if (label == NULL)
    {
        addError(breakStatement, ERR_CC_BREAK_OUTSIDE_OF_LOOP);
        return;
    }
    breakStatement->label = label;
}


void SemanticCheckVisitor::visit(astnodes::Expression * expression)
{
    throw new errors::InternalCompilerException("Expression Node is only abstract and cannot be visited");
}


void SemanticCheckVisitor::visit(astnodes::Statement * statement)
{
    throw new errors::InternalCompilerException("Statement Node is only abstract and cannot be visited");
}


void SemanticCheckVisitor::visit(astnodes::ExternalDeclaration * externalDeclaration)
{
    throw new errors::InternalCompilerException("ExternalDeclaration Node is only abstract and cannot be visited");
}


void SemanticCheckVisitor::visit(astnodes::ReturnStatement * returnStatement)
{
    // first check the expression
    returnStatement->allChildrenAccept(*this);
    // then check if it is compatible with the function return value
    // TODO TODO FIXME
    // TODO  get the label to the end of the function to jump to
}


void SemanticCheckVisitor::visit(astnodes::ForStatement * forStatement)
{
    // Create labels for the for statement:
    forStatement->startLbl = getRandomLabel("for_start");
    forStatement->endLbl = getRandomLabel("for_end");
    forStatement->continueLbl = getRandomLabel("for_continue");
    // put the labels on the loop stack
    pushLoopStack(forStatement->endLbl, forStatement->continueLbl);
    // evaluate all children
    forStatement->allChildrenAccept(*this);
    
    // check that the expression type is a scalar type
    if(!types::IsTypeHelper::isScalarType(forStatement->condExpr->expr->valType->type))
    {
        addError(forStatement, ERR_CC_EXPECTED_SCALAR_FOR);
    }
    
    // pop loop stack
    popLoopStack();
}


void SemanticCheckVisitor::visit(astnodes::DoWhileStatement * doWhileStatement)
{
    // Create labels for the for statement:
    doWhileStatement->startLbl = getRandomLabel("dowhile_start");
    doWhileStatement->endLbl = getRandomLabel("dowhile_end");
    doWhileStatement->continueLbl = getRandomLabel("dowhile_continue");
    
    // put the labels on the loop stack
    pushLoopStack(doWhileStatement->endLbl, doWhileStatement->continueLbl);
    // evaluate all children
    doWhileStatement->allChildrenAccept(*this);
    
    // check that the expression type is a scalar type
    if(!types::IsTypeHelper::isScalarType(doWhileStatement->condExpr->valType->type))
    {
        addError(doWhileStatement, ERR_CC_EXPECTED_SCALAR_DOWHILE);
    }
    
    // pop loop stack
    popLoopStack();
}


void SemanticCheckVisitor::visit(astnodes::WhileStatement * whileStatement)
{
    // Create labels for the for statement:
    whileStatement->startLbl = getRandomLabel("while_start");
    whileStatement->endLbl = getRandomLabel("while_end");
    
    // put the labels on the loop stack
    pushLoopStack(whileStatement->endLbl, whileStatement->startLbl);
    // evaluate all children
    whileStatement->allChildrenAccept(*this);
    
    // check that the expression type is a scalar type
    if(!types::IsTypeHelper::isScalarType(whileStatement->condExpr->valType->type))
    {
        addError(whileStatement, ERR_CC_EXPECTED_SCALAR_WHILE);
    }
    
    // pop loop stack
    popLoopStack();
}


void SemanticCheckVisitor::visit(astnodes::SwitchStatement * switchStatement)
{
    // first evaluate the controlling expression
    switchStatement->expr->accept(*this);
    
    // check for integral type
    if(!types::IsTypeHelper::isIntegralType(switchStatement->expr->valType->type))
    {
        addError(switchStatement, ERR_CC_EXPECTED_INTEGRAL_SWITCH);
        // don't handle anything inside the switch statement
        return;
    }
    
    // set promoted type
    switchStatement->promotedType = switchStatement->expr->valType->type;
    
    // push switch statement on switch-statement stack
    this->m_switchStack.push_back(switchStatement);
    
    // now check the block statement
    switchStatement->statement->accept(*this);
    
    // pop switch statement stack
    this->m_switchStack.pop_back();
    
    // check if this is still an integral type
    if(!types::IsTypeHelper::isIntegralType(switchStatement->promotedType))
    {
        addError(switchStatement, ERR_CC_EXPECTED_INTEGRAL_SWITCH);
    }
}


void SemanticCheckVisitor::visit(astnodes::IfStatement * ifStatement)
{
    // first evaluate the controlling expression
    ifStatement->condExpr->accept(*this);
    
    // check for integral type
    if(!types::IsTypeHelper::isScalarType(ifStatement->condExpr->valType->type))
    {
        addError(ifStatement, ERR_CC_EXPECTED_SCALAR_IF);
        // don't handle anything inside the if statement
        return;
    }
    
    // Create labels for the if statement:
    ifStatement->endlbl = getRandomLabel("if_end");
    if (ifStatement->elseStmt != NULL)
        ifStatement->elselbl = getRandomLabel("if_else");
    
    // recurse into the statements
    ifStatement->ifStmt->accept(*this);
    if (ifStatement->elseStmt != NULL)
        ifStatement->elseStmt->accept(*this);
}


void SemanticCheckVisitor::visit(astnodes::ExpressionStatement * expressionStatement)
{
    // just pass through
    expressionStatement->allChildrenAccept(*this);
}


void SemanticCheckVisitor::visit(astnodes::EmptyStatement * emptyStatement)
{
    // nothing to do here
}


void SemanticCheckVisitor::visit(astnodes::BlockStatement * blockStatement)
{
    // create new scope, or if this is the main block of a function
    // then use the scope already filled with the function parameters
    if (blockStatement->scope == NULL)
        blockStatement->scope = m_symbolTable->beginNewScope();
    else
        m_symbolTable->beginScope(blockStatement->scope);
    
    // analyse the statements 
    blockStatement->allChildrenAccept(*this);
    
    // end the scope
    m_symbolTable->endScope();
}


void SemanticCheckVisitor::visit(astnodes::DefaultStatement * defaultStatement)
{
    if (m_switchStack.empty())
    {
        addError(defaultStatement, ERR_CC_DEFAULT_OUTSIDE_OF_SWITCH);
        // don't handle anything inside the default statement
        return;
    }
    
    if (m_switchStack.back()->defaultLbl != NULL)
    {
        addError(defaultStatement, ERR_CC_MULTIPLE_DEFAULT);
        // don't handle anything inside the default statement
        return;
    }
    
    defaultStatement->lbl = getRandomLabel("default");
    
    m_switchStack.back()->defaultLbl = defaultStatement->lbl;
    
    // analyse child statement:
    defaultStatement->allChildrenAccept(*this);
}


void SemanticCheckVisitor::visit(astnodes::CaseStatement * caseStatement)
{
    // TODO const expression evaluator
    // TODO
    // TODO the expression evaluator (first finish semantic check for expressions)
    printAstName("CaseStatement");
    caseStatement->allChildrenAccept(*this);
}


void SemanticCheckVisitor::visit(astnodes::LabelStatement * labelStatement)
{
    // look if a label with the same name already exists
    std::map<std::string, astnodes::LabelStatement*>::iterator it;
    if ((it = this->m_funcLabels.find(labelStatement->label)) != this->m_funcLabels.end())
    {
        addError(labelStatement, ERR_CC_DUPLICATE_LABEL, labelStatement->label);
        addError(it->second, ERR_CC_NOTE_DUPLICATE_LABEL, labelStatement->label);
        // don't handle anything inside the default statement
        return;
    }
    
    // insert label into the map
    this->m_funcLabels[labelStatement->label] = labelStatement;
    
    // go on analysing the statement after the label:
    labelStatement->allChildrenAccept(*this);
}






/*######################################*/
/*        3.5 DECLARATIONS              */
/*######################################*/

/*
 *  TODO implement all this:
 *  TODO
 *  How declarations are handled:
 *    The Declaration consists of multiple declarators.
 *    those are recursively consisting of more declarators.
 * 
 *    To desolve the type of a declaration, the declarators recursively
 *    add to the basic type calculated by the declaration.
 *  
 *  How storage specifiers are handled:
 *    Variables:
 *      Typedef: add to current scope -> TAGS
 *      Extern:  add to current scope as "ghost" (no space allocation)
 *               IdentifierDeclarator in CodeGen: add to global space:
 *                      .IMPORT cglob_<varname>
 *      Static:  add to current scope as global
 *               IdentifierDeclarator in CodeGen; add to global space:
 *                      _cstatic_<varname> DAT 0x0, ...
 *      Other:   add to current scope as local or global if global scope
 *               IdentifierDeclarator in CodeGen, if global add to global space:
 *                      .EXPORT cglob_<varname>
 *                      cglob_<varname>: DAT 0x0, ...
 *
 *    Functions: (Function Declaration AND Definition)
 *       Static: -> no .EXPORT
 *       Default: -> .EXPORT for definition, .IMPORT for declaration
 *          => this is handled on a global basis via the symbol table
 *       
 *  How initializers are handled:
 *    They are passed down recursively to the final identifierDeclarator.
 *     This is where they are type checked, (and in mismatch case converted).
 *    During CodeGen, the identifierDeclarator implements the initialization of the
 *     variable, or the array.
 * 
 * 
 */


void SemanticCheckVisitor::visit(astnodes::Declaration * declaration)
{
    // get type from declaration specifiers
    types::Type* declType = declSpecsToType(declaration->declSpecifiers);
    
    // next check for the storage specifiers
    if (declaration->declSpecifiers->storageSpecifiers.size() > 1)
    {
        addError(declaration->declSpecifiers->storageSpecifiers.front(), ERR_CC_STORSPEC_CONFL);
        return;
    } else if (declaration->declSpecifiers->storageSpecifiers.size() == 1)
    {
        switch(declaration->declSpecifiers->storageSpecifiers.front()->token)
        {
            case TYPEDEF:
                declaration->storageSpecifier = astnodes::STORAGE_TYPEDEF;
                break;
            case EXTERN:
                declaration->storageSpecifier = astnodes::STORAGE_EXTERN;
                break;
            case STATIC:
                declaration->storageSpecifier = astnodes::STORAGE_STATIC;
                break;
            case AUTO:
                declaration->storageSpecifier = astnodes::STORAGE_AUTO;
                break;
            case REGISTER:
                declaration->storageSpecifier = astnodes::STORAGE_REGISTER;
                break;
            default:
                throw new errors::InternalCompilerException("unknown storage specifier encountered");
        }
    }
    else
    {
        // empty storage specifiers
        declaration->storageSpecifier = astnodes::STORAGE_DEFAULT;
    }
    
    // back up declaration variables
    types::Type* oldDeclType = this->m_curDeclType;
    astnodes::StorSpec_t oldStorSpec = this->m_curStorSpec;
    astnodes::Expressions* oldInits = this->m_declarationInitializer;
    
    // set new declaration variables
    this->m_curDeclType = declType;
    this->m_curStorSpec = declaration->storageSpecifier;
    
    // now visit all the declarators
    if (!declaration->isParamDecl)
    {
        for (astnodes::Declarators::iterator i = declaration->declarators->begin(); i != declaration->declarators->end(); ++i)
        {
            // set initializers
            this->m_declarationInitializer = (*i)->initializers;
            
            // call declarators recursively
            (*i)->accept(*this);
        }
    }
    else
    {
        // set initializers
        this->m_declarationInitializer = declaration->singleDeclarator->initializers;
        
        // call declarator recursively in order to get the actual declaration type
        declaration->singleDeclarator->accept(*this);
        
        // get type and name of actual declarator
        types::Type* actualDeclType = this->m_curDeclType;
        
        // set the associated type
        declaration->type = actualDeclType;
    }
    
    // restore declaration variables
    this->m_curDeclType = oldDeclType;
    this->m_curStorSpec = oldStorSpec;
    this->m_declarationInitializer = oldInits;
}




/******************************/
/*     3.5.4 Declarators      */
/******************************/

void SemanticCheckVisitor::visit(astnodes::Declarator * declarator)
{
    throw new errors::InternalCompilerException("Declarator Node is only abstract and cannot be visited");
}

void SemanticCheckVisitor::visit(astnodes::NoIdentifierDeclarator * noIdentifierDeclarator)
{
    // add pointers
    noIdentifierDeclarator->allChildrenAccept(*this);
    
    if (m_declState == DECLSTATE_PARAM)
    {
        addError(noIdentifierDeclarator, ERR_CC_FUNC_PARAM_NO_NAME);
    }
}

void SemanticCheckVisitor::visit(astnodes::IdentifierDeclarator * identifierDeclarator)
{
    // add pointers
    if (identifierDeclarator->pointers != NULL)
        for (astnodes::Pointers::iterator i = identifierDeclarator->pointers->begin(); i != identifierDeclarator->pointers->end(); ++i)
            (*i)->accept(*this);
        
    // get name of the declaration
    std::string declName = identifierDeclarator->name;
    
    // get type for the declaration
    types::Type* actualDeclType = this->m_curDeclType;
    types::Type* type = actualDeclType;
    
    // TODO maybe set type as member of identifierDeclarator
    
    // check if this was enough
    if (m_declState == DECLSTATE_TYPE_ONLY)
        return;
    
    if (m_declState == DECLSTATE_FUNDEF)
    {
        // return the name
        m_functionName = declName;
        return;
    }
    
    // insert into symbol table based on storage specs
    if (types::IsTypeHelper::isFunctionType(actualDeclType))
    {
        // check that this is not a parameter type
        if (m_declState == DECLSTATE_PARAM)
        {
            addError(identifierDeclarator, ERR_CC_PARAMTER_FUNCTION_TYPE);
            return;
        }
        
        // check that this does not have initializers
        if (m_declarationInitializer != NULL)
        {
            addError(identifierDeclarator, ERR_CC_INIT_FOR_FUNCTION);
            return;
        }
        
        // this is a function declaration
        // check for invalid storage specs
        switch(m_curStorSpec)
        {
            case astnodes::STORAGE_TYPEDEF:
            case astnodes::STORAGE_REGISTER:
            case astnodes::STORAGE_AUTO:
                addError(identifierDeclarator, ERR_CC_FUNC_RETURN_STORAGE, declName);
                return;
                break;
                
            case astnodes::STORAGE_STATIC:
            case astnodes::STORAGE_EXTERN:
            case astnodes::STORAGE_DEFAULT:
                // everything fine
                break;
            default:
                throw new errors::InternalCompilerException("unknown storage specifier encountered");
        }
        
        // put encountered function into symbol table
        symboltable::SymbolObject obj;
        // check if function already in scope
        if(this->m_symbolTable->getGlobalScope().findSymbol(declName, obj))
        {
            if (obj.declType == symboltable::FUNCTION_DEF)
            {
                addError(identifierDeclarator, ERR_CC_REDECL, declName);
                return;
            }
            else if (obj.declType != symboltable::FUNCTION_DECL)
            {
                addError(identifierDeclarator, ERR_CC_REDECL, declName);
                return;
            }
        }
        
        // put into symbol table as GLOBAL or STATIC
        switch(m_curStorSpec)
        {
            case astnodes::STORAGE_STATIC:
                this->m_symbolTable->getGlobalScope().insertSymbol(declName, symboltable::FUNCTION_DECL, actualDeclType, symboltable::STATIC_FUNC);
                break;
            case astnodes::STORAGE_EXTERN:
            case astnodes::STORAGE_DEFAULT:
                this->m_symbolTable->getGlobalScope().insertSymbol(declName, symboltable::FUNCTION_DECL, actualDeclType, symboltable::GLOBAL);
                break;
            default:
                throw new errors::InternalCompilerException("unknown storage specifier encountered");
        }
    }
    else if (m_declState == DECLSTATE_PARAM)
    {
        // check that the function parameter declaration doesn't use any 
        // storage specifiers besides `register`
        switch(m_curStorSpec)
        {
            case astnodes::STORAGE_TYPEDEF:
            case astnodes::STORAGE_EXTERN:
            case astnodes::STORAGE_STATIC:
            case astnodes::STORAGE_AUTO:
                addError(identifierDeclarator, ERR_CC_FUNC_PARAM_STORAGE, declName);
                return;
            case astnodes::STORAGE_REGISTER:
            case astnodes::STORAGE_DEFAULT:
                // everything is fine
                break;
            default:
                throw new errors::InternalCompilerException("unknown storage specifier encountered");
        }
        // check parameters don't use initializers
        if (m_declarationInitializer != NULL)
        {
            addError(identifierDeclarator, ERR_CC_INIT_FOR_PARAM);
            return;
        }
        
        if (!types::IsTypeHelper::isVoid(actualDeclType))
        {
            if (m_symbolTable->getCurrentScope().containsTag(declName))
            {
                addError(identifierDeclarator, ERR_CC_REDECL, declName);
                return;
            }
            
            // save as parameter in current scope
            m_symbolTable->getCurrentScope().insertSymbol(declName, symboltable::VARIABLE_DECL, actualDeclType, symboltable::PARAMETER_STACK);
        }
        return;
    }
    else
    {
        /* this is a Variable Declaration */
        
        // check for re-declaration
        switch(m_curStorSpec)
        {
            case astnodes::STORAGE_TYPEDEF:
                // check if the symbol has been declared before
                if (m_symbolTable->getCurrentScope().containsTag(declName))
                {
                    addError(identifierDeclarator, ERR_CC_REDECL, declName);
                    return;
                }
                break;
            case astnodes::STORAGE_EXTERN:
            case astnodes::STORAGE_STATIC:
            case astnodes::STORAGE_AUTO:
            case astnodes::STORAGE_REGISTER:
            case astnodes::STORAGE_DEFAULT:
                // check if the symbol has been declared before
                if (m_symbolTable->getCurrentScope().containsSymbol(declName))
                {
                    addError(identifierDeclarator, ERR_CC_REDECL, declName);
                    return;
                }
                break;
            default:
                throw new errors::InternalCompilerException("unknown storage specifier encountered");
        }
        
        // put encountered symbol into symbol table
        switch(m_curStorSpec)
        {
            case astnodes::STORAGE_TYPEDEF:
                // put in typedef table
                m_symbolTable->getCurrentScope().insertTag(declName, symboltable::TYPEDEF_TAG, actualDeclType);
                break;
            case astnodes::STORAGE_EXTERN:
                m_symbolTable->getCurrentScope().insertSymbol(declName, symboltable::VARIABLE_DECL, actualDeclType, symboltable::POS_EXTERN);
                break;
            case astnodes::STORAGE_STATIC:
                m_symbolTable->getCurrentScope().insertSymbol(declName, symboltable::VARIABLE_DECL, actualDeclType, symboltable::GLOBAL);
                break;
            case astnodes::STORAGE_AUTO:
                // this is the default storage for declarations:
            case astnodes::STORAGE_REGISTER:
                // handle register just normally as well
                // just fucking ignore the request for register storage for now :P
                // TODO maybe handle it specially some time ;P
            case astnodes::STORAGE_DEFAULT:
                if (m_symbolTable->isGlobalCurScope())
                    // put onto global storage
                    m_symbolTable->getCurrentScope().insertSymbol(declName, symboltable::VARIABLE_DECL, actualDeclType, symboltable::GLOBAL);
                else
                    // put onto local stack symbol table
                    m_symbolTable->getCurrentScope().insertSymbol(declName, symboltable::VARIABLE_DECL, actualDeclType, symboltable::LOCAL_STACK);
                break;
            default:
                throw new errors::InternalCompilerException("unknown storage specifier encountered");
        }
    }
    
    
    // get initializers
    identifierDeclarator->initializers = m_declarationInitializer;
    
    // check for conditions for the initializers
    if (types::IsTypeHelper::isFunctionType(type))
    {
        if (identifierDeclarator->initializers != NULL)
        {
            if (identifierDeclarator->initializers->size() > 0)
            {
                addError(identifierDeclarator, ERR_CC_INIT_FOR_FUNCTION, identifierDeclarator->name);
                return;
            }
        }
    }
    else if (types::IsTypeHelper::isArrayType(type))
    {
        types::ArrayType* arrType = types::IsTypeHelper::getArrayType(type);
        if (identifierDeclarator->initializers != NULL)
        {
            if (identifierDeclarator->initializers->size() > arrType->size)
            {
                addWarning(identifierDeclarator, WARN_CC_INIT_LIST_TOO_LONG, identifierDeclarator->name);
            }
            
            // visit expressions recursively to resolve their type
            for (astnodes::Expressions::iterator i = identifierDeclarator->initializers->begin(); i != identifierDeclarator->initializers->end(); ++i)
                (*i)->accept(*this);
            
            // TODO check for compatible types
        }
    }
    else
    {
        // this is a "normal" variable then
        if (identifierDeclarator->initializers != NULL)
        {
            // check that there is at most 1 initializer
            if (identifierDeclarator->initializers->size() > 1)
            {
                addError(identifierDeclarator, ERR_CC_TOO_MANY_INITS, identifierDeclarator->name);
                return;
            }
            
            if (identifierDeclarator->initializers->size() == 1)
            {
                // visit the expression
                identifierDeclarator->initializers->front()->accept(*this);
                
                // now check for compatible types
                // TODO
            }
        }
    }
}


/* 3.5.4.1 Pointer declarators */

void SemanticCheckVisitor::visit(astnodes::Pointer * pointer)
{
    // get base type
    types::Type* baseType = this->m_curDeclType;
    // construct pointer type with that base type
    types::Type* result = new types::PointerType(baseType);
    // now check for type qualifiers and add them to the pointer type
    for (std::vector<astnodes::TypeQualifier*>::iterator i = pointer->typeQualifiers->begin(); i != pointer->typeQualifiers->end(); ++i)
    {
        switch ((*i)->token)
        {
            case CONST:
                result->isConst = true;
                break;
            case VOLATILE:
                result->isVolatile = true;
                break;
            default:
                throw new errors::InternalCompilerException("unknown type qualifier encountered");
        }
    }
    
    // now push new type back on the stack
    this->m_curDeclType = result;
}


/* 3.5.4.2 Array declarators */

void SemanticCheckVisitor::visit(astnodes::ArrayDeclarator * arrayDeclarator)
{
    // first add pointers to the type 
    if (arrayDeclarator->pointers != NULL)
        for (astnodes::Pointers::iterator i = arrayDeclarator->pointers->begin(); i != arrayDeclarator->pointers->end(); ++i)
            (*i)->acceptPostRecursive(*this);
    
    // get base type
    types::Type* baseType = this->m_curDeclType;
    types::Type* result = NULL;
    
    long size = -1;
    if (arrayDeclarator->constExpr != NULL)
    {
        // first visit the expression with this visitor
        arrayDeclarator->constExpr->accept(*this);
        
        // get size 
        if (! valuetypes::IsValueTypeHelper::isCValue(arrayDeclarator->constExpr->valType))
        {
            addError(arrayDeclarator, ERR_CC_ARRAY_SIZE_NOT_CONST);
            this->m_curDeclType = new types::InvalidType();
            return;
        }
        
        ConstExprEvalVisitor ceval;
        arrayDeclarator->constExpr->accept(ceval);
        size = valuetypes::ConstHelper::getIntegralConst(arrayDeclarator->constExpr->valType);
        
        // TODO maybe support GNU C like arrays of size 0
        if (size < 1)
        {
            addError(arrayDeclarator, ERR_CC_ARRAY_SIZE_NOT_POS);
            this->m_curDeclType = new types::InvalidType();
            return;
        }
    }
    else
    {
        if (m_declState == DECLSTATE_LOCAL || m_declState == DECLSTATE_GLOBAL)
        {
            addError(arrayDeclarator, ERR_CC_ARRAY_SIZE_MISSING);
            this->m_curDeclType = new types::InvalidType();
            return;
        }
        size = -1;
    }
    
    if (!baseType->isComplete())
    {
        addError(arrayDeclarator, ERR_CC_ARRAY_INCOMPL_TYPE);
        this->m_curDeclType = new types::InvalidType();
        return;
    }
    
    uint16_t typeSize = baseType->getWordSize();
    
    result = new types::ArrayType(baseType, (int) size, typeSize);
    this->m_curDeclType = result;
    
    // visit recursively
    arrayDeclarator->baseDeclarator->accept(*this);
}


/* 3.5.4.3 Function declarators */

void SemanticCheckVisitor::visit(astnodes::FunctionDeclarator * functionDeclarator)
{
    // first add pointers to the type 
    if (functionDeclarator->pointers != NULL)
        for (astnodes::Pointers::iterator i = functionDeclarator->pointers->begin(); i != functionDeclarator->pointers->end(); ++i)
            (*i)->acceptPostRecursive(*this);
    
    // get return type
    types::Type* returnType = this->m_curDeclType;
    
    // check that return type is not a function
    if (types::IsTypeHelper::isFunctionType(returnType))
    {
        addError(functionDeclarator, ERR_CC_FUNCDECL_RETURN_FUNCT);
        this->m_curDeclType = new types::InvalidType();
        return;
    }
    
    // check that return type is not an array
    if (types::IsTypeHelper::isArrayType(returnType))
    {
        addError(functionDeclarator, ERR_CC_FUNCDECL_RETURN_ARRAY);
        this->m_curDeclType = new types::InvalidType();
        return;
    }
    
    // backup declaration variables
    DeclarationState_t oldDeclState = m_declState;
    
    if (m_declState == DECLSTATE_FUNDEF)
    {
        // in case of a function definition, save the parameters
        m_declState = DECLSTATE_PARAM;
        
        // also there might have been another Function declarator before
        // (in case that the a function definition has a function pointer as
        //  return value)
        // in that case, clear the current scope
        m_symbolTable->getCurrentScope().clearScope();
    }
    else
    {
        // otherwise, just get me the type
        m_declState = DECLSTATE_TYPE_ONLY;
    }
    
    // visit all declarators in the parameter list
    for (std::vector<astnodes::Declaration*>::iterator i = functionDeclarator->parameterList->declarations.begin(); i != functionDeclarator->parameterList->declarations.end(); ++i)
    {
        (*i)->accept(*this);
    }
    
    // restore decl state
    m_declState = oldDeclState;
    
    // get the type list
    types::ParameterTypeList* paramTypes = new types::ParameterTypeList();
    

    for (std::vector<astnodes::Declaration*>::iterator i = functionDeclarator->parameterList->declarations.begin(); i != functionDeclarator->parameterList->declarations.end(); ++i)
    {
        // check that it is a valid type
        if (types::IsTypeHelper::isInvalidType((*i)->type))
        {
            // in this case error message has already been output
            this->m_curDeclType = new types::InvalidType();
            return;
        }
        
        // add to param list
        paramTypes->push_back((*i)->type);
    }
    
    // check if this is a paramter list like:  foo(void)
    if (paramTypes->size() == 1
        && types::IsTypeHelper::isVoid(paramTypes->back()))
    {
        // only one void paramter -> same as empty parameter list
        paramTypes->clear();
    }
    
    // get var args 
    bool varargs = functionDeclarator->parameterList->varArgs;
    
    // now we have everything to construct the function type
    types::Type* funtype = new types::FunctionType(returnType, paramTypes, varargs);
    
    // push type
    this->m_curDeclType = funtype;
    
    // visit recursively
    functionDeclarator->baseDeclarator->accept(*this);
}




/**********************************/
/*       3.5.5 Type names         */
/**********************************/

// the type for cast and size of expression
void SemanticCheckVisitor::visit(astnodes::TypeName * typeName)
{
    // get type from declaration specifiers
    types::Type* declType = declSpecsToType(typeName->declSpecifiers);
    
    if (typeName->abstrDeclarator != NULL)
    {
        // back up declaration variables
        types::Type* oldDeclType = this->m_curDeclType;
        astnodes::StorSpec_t oldStorSpec = this->m_curStorSpec;
        astnodes::Expressions* oldInits = this->m_declarationInitializer;
        DeclarationState_t oldDeclState = this->m_declState;
        
        // set new declaration variables
        this->m_curDeclType = declType;
        this->m_declState = DECLSTATE_TYPE_ONLY;

        typeName->abstrDeclarator->accept(*this);
        
        // get type of actual declarator
        typeName->type = m_curDeclType;
        
        // restore declaration variables
        this->m_curDeclType = oldDeclType;
        this->m_curStorSpec = oldStorSpec;
        this->m_declarationInitializer = oldInits;
        this->m_declState = oldDeclState;
    }
    else
    {
        typeName->type = declType;
    }
}











/**********************************/
/* 3.5.1 Storage-class specifiers */
/**********************************/

void SemanticCheckVisitor::visit(astnodes::StorageSpecifier * storageSpecifier)
{
    throw new errors::InternalCompilerException("a storage specifier should not be visited");
}




/******************************/
/*  3.5.2 Type specifiers     */
/******************************/


types::Type* SemanticCheckVisitor::declSpecsToType(astnodes::DeclarationSpecifiers* declSpecs)
{
    
    // first get the type from the type specifier
    resetDeclSpecs();
    // visit all type specifiers
    for (std::vector<astnodes::TypeSpecifier*>::iterator i = declSpecs->typeSpecifiers.begin(); i != declSpecs->typeSpecifiers.end(); ++i)
    {
        (*i)->accept(*this);
    }
    // now get the type
    types::Type* declType = typeSpecsToType(declSpecs->typeSpecifiers.front());
    // is it a valid type?
    if (types::IsTypeHelper::isInvalidType(declType))
        // errors have already been generated in this case, just ignore this declaration
        return new types::InvalidType();
    
    // now check for type qualifiers
    for (std::vector<astnodes::TypeQualifier*>::iterator i = declSpecs->typeQualifiers.begin(); i != declSpecs->typeQualifiers.end(); ++i)
    {
        switch ((*i)->token)
        {
            case CONST:
                declType->isConst = true;
                break;
            case VOLATILE:
                declType->isVolatile = true;
                break;
            default:
                throw new errors::InternalCompilerException("unknown type qualifier encountered");
        }
    }
    
    return declType;
}



void SemanticCheckVisitor::resetDeclSpecs()
{
    declSpecs.isAdvType = false;
    declSpecs.type = NULL;
    declSpecs.isVoid = false;
    declSpecs.isChar = false;
    declSpecs.isSigned = false;
    declSpecs.isUnsigned = false;
    declSpecs.isInt = false;
    declSpecs.isLong = false;
    declSpecs.isShort = false;
    declSpecs.isFloat = false;
    declSpecs.isDouble = false;
}

types::Type* SemanticCheckVisitor::typeSpecsToType(astnodes::Node* decl)
{
    types::Type* result = NULL;
    
    // check for non-basic types
    if (declSpecs.isAdvType && declSpecs.type != NULL)
    {
        result = declSpecs.type;
    }
    // now check for all the basic types
    else if (declSpecs.isVoid)
    {
        result = new types::Void();
        declSpecs.isVoid = false;
    }
    else if (declSpecs.isFloat)
    {
        result = new types::Float();
        declSpecs.isFloat = false;
    }
    else if (declSpecs.isDouble && declSpecs.isLong)
    {
        result = new types::LongDouble();
        declSpecs.isDouble = false;
        declSpecs.isLong = false;
    }
    else if (declSpecs.isDouble)
    {
        result = new types::Double();
        declSpecs.isDouble = false;
    }
    else if (declSpecs.isChar && declSpecs.isSigned)
    {
        result = new types::SignedChar();
        declSpecs.isSigned = false;
        declSpecs.isChar = false;
    }
    else if (declSpecs.isChar)
    {
        result = new types::UnsignedChar();
        declSpecs.isUnsigned = false;
        declSpecs.isChar = false;
    }
    else if (declSpecs.isShort && declSpecs.isUnsigned)
    {
        result = new types::UnsignedShort();
        declSpecs.isUnsigned = false;
        declSpecs.isShort = false;
        declSpecs.isInt = false;
    }
    else if (declSpecs.isShort)
    {
        result = new types::SignedShort();
        declSpecs.isSigned = false;
        declSpecs.isShort = false;
        declSpecs.isInt = false;
    }

    else if (declSpecs.isLong && declSpecs.isUnsigned)
    {
        result = new types::UnsignedLong();
        declSpecs.isUnsigned = false;
        declSpecs.isLong = false;
        declSpecs.isInt = false;
    }
    else if (declSpecs.isLong)
    {
        result = new types::SignedLong();
        declSpecs.isSigned = false;
        declSpecs.isLong = false;
        declSpecs.isInt = false;
    }
    else if (declSpecs.isUnsigned)
    {
        result = new types::UnsignedInt();
        declSpecs.isUnsigned = false;
        declSpecs.isInt = false;
    }
    else if (declSpecs.isInt)
    {
        result = new types::SignedInt();
        declSpecs.isSigned = false;
        declSpecs.isInt = false;
    }
    else
    {
        addError(decl, ERR_CC_DECLSPEC_NO_TYPE);
        return new types::InvalidType();
    }
    
    // if any of the bools are still set, we've got a conflict
    if (   declSpecs.isVoid
        || declSpecs.isChar
        || declSpecs.isSigned
        || declSpecs.isUnsigned
        || declSpecs.isInt
        || declSpecs.isLong
        || declSpecs.isShort
        || declSpecs.isFloat
        || declSpecs.isDouble)
    {
        addError(decl, ERR_CC_DECLSPEC_CONFL);
        return new types::InvalidType();
    }
    
    if (result == NULL)
        throw new errors::InternalCompilerException("something weird has happend during resolution of type specifiers");
    
    return result;
}

void SemanticCheckVisitor::visit(astnodes::TypeSpecifier * typeSpecifier)
{
    throw new errors::InternalCompilerException("Declarator Node is only abstract and cannot be visited");
}

void SemanticCheckVisitor::visit(astnodes::TypeBaseSpecifier * typeBaseSpecifier)
{
    switch(typeBaseSpecifier->token)
    {
        case VOID:
            if (declSpecs.isVoid)
                addError(typeBaseSpecifier, ERR_CC_DECLSPEC_VOID_DUPL);
            else
                declSpecs.isVoid = true;
            break;
        case CHAR:
            if (declSpecs.isChar)
                addError(typeBaseSpecifier, ERR_CC_DECLSPEC_CHAR_DUPL);
            else
                declSpecs.isChar = true;
            break;
        case SHORT:
            if (declSpecs.isShort)
                addError(typeBaseSpecifier, ERR_CC_DECLSPEC_SHORT_DUPL);
            else
                declSpecs.isShort = true;
            break;
        case INT:
            if (declSpecs.isInt)
                addError(typeBaseSpecifier, ERR_CC_DECLSPEC_INT_DUPL);
            else
                declSpecs.isInt = true;
            break;
        case LONG:
            if (declSpecs.isLong)
                addError(typeBaseSpecifier, ERR_CC_DECLSPEC_LONG_DUPL);
            else
                declSpecs.isLong = true;
            break;
        case FLOAT:
            if (declSpecs.isFloat)
                addError(typeBaseSpecifier, ERR_CC_DECLSPEC_FLOAT_DUPL);
            else
                declSpecs.isFloat = true;
            break;
        case DOUBLE:
            if (declSpecs.isDouble)
                addError(typeBaseSpecifier, ERR_CC_DECLSPEC_DOUBLE_DUPL);
            else
                declSpecs.isDouble = true;
            break;
        case SIGNED:
            if (declSpecs.isSigned)
                addError(typeBaseSpecifier, ERR_CC_DECLSPEC_SIGNED_DUPL);
            else if (declSpecs.isUnsigned)
                addError(typeBaseSpecifier, ERR_CC_DECLSPEC_CONFL_SIGNED);
            else
                declSpecs.isSigned = true;
            break;
        case UNSIGNED:
            if (declSpecs.isUnsigned)
                addError(typeBaseSpecifier, ERR_CC_DECLSPEC_UNSIGNED_DUPL);
            else if (declSpecs.isSigned)
                addError(typeBaseSpecifier, ERR_CC_DECLSPEC_CONFL_SIGNED);
            else
                declSpecs.isUnsigned = true;
            break;
        default:
            throw new errors::InternalCompilerException("unkown type specifier encountered");
    }
}



void SemanticCheckVisitor::visit(astnodes::StructUnionSpecifier * structUnionSpecifier)
{
    printAstName("StructUnionSpecifier");
    structUnionSpecifier->allChildrenAccept(*this);
}


void SemanticCheckVisitor::visit(astnodes::EnumSpecifier * enumSpecifier)
{
    printAstName("EnumSpecifier");
    enumSpecifier->allChildrenAccept(*this);
}



void SemanticCheckVisitor::visit(astnodes::TypeNameSpecifier * typeNameSpecifier)
{
    types::Type* type = m_symbolTable->getTagType(typeNameSpecifier->name);
    
    if (type == NULL)
    {
        addError(typeNameSpecifier, ERR_CC_INVALID_TYPENAME, typeNameSpecifier->name);
        return;
    }
    
    declSpecs.isAdvType = true;
    declSpecs.type = type;
}


/******************************/
/*  3.5.3 Type qualifiers     */
/******************************/

void SemanticCheckVisitor::visit(astnodes::TypeQualifier * typeQualifier)
{
    throw new errors::InternalCompilerException("a type qualifier should not be visited");
}



/*######################################*/
/*      3.3 Expressions                 */
/*######################################*/


/******************************/
/*  3.3.1 Primary expressions */
/******************************/

/* identifier */

void SemanticCheckVisitor::visit(astnodes::Identifier * identifier)
{
    if (!this->m_symbolTable->containsRec(identifier->name))
    {
        addError(identifier, ERR_CC_VARIABLE_NOT_IN_SCOPE, identifier->name);
        identifier->valType = getInvalidValType();
        // don't handle anything below
        return;
    }
    
    // TODO enum types are constants!! 
    
    types::Type* varType = this->m_symbolTable->getTypeOfVariable(identifier->name);
    if (varType == NULL)
    {
        // this must be a function then
        types::FunctionType* funcType = this->m_symbolTable->getFunction(identifier->name);
        if (funcType == NULL)
        {
            throw new errors::InternalCompilerException("Neither a variable nor a function was found by the name '" + identifier->name + "'.");
        }
        identifier->valType = new valuetypes::FunctionDesignator(funcType);
    }
    else
    {
        identifier->valType = new valuetypes::LValue(varType);
    }
    
    // assign typeposition
    identifier->typePos = this->m_symbolTable->getPositionOfVariable(identifier->name);
}


/* constants */

void SemanticCheckVisitor::visit(astnodes::CharacterLiteral * characterLiteral)
{
    characterLiteral->valType = new valuetypes::CValue(new types::UnsignedChar());
}


void SemanticCheckVisitor::visit(astnodes::SignedIntLiteral * signedIntLiteral)
{
    signedIntLiteral->valType = new valuetypes::CValue(new types::SignedInt());
}


void SemanticCheckVisitor::visit(astnodes::UnsignedIntLiteral * unsignedIntLiteral)
{
    unsignedIntLiteral->valType = new valuetypes::CValue(new types::UnsignedInt());
}


void SemanticCheckVisitor::visit(astnodes::SignedLongLiteral * signedLongLiteral)
{
    signedLongLiteral->valType = new valuetypes::CValue(new types::SignedLong());
}


void SemanticCheckVisitor::visit(astnodes::UnsignedLongLiteral * unsignedLongLiteral)
{
    unsignedLongLiteral->valType = new valuetypes::CValue(new types::UnsignedLong());
}


void SemanticCheckVisitor::visit(astnodes::FloatLiteral * floatLiteral)
{
    floatLiteral->valType = new valuetypes::CValue(new types::Float());
}


void SemanticCheckVisitor::visit(astnodes::DoubleLiteral * doubleLiteral)
{
    doubleLiteral->valType = new valuetypes::CValue(new types::Double());
}


void SemanticCheckVisitor::visit(astnodes::LongDoubleLiteral * longDoubleLiteral)
{
    longDoubleLiteral->valType = new valuetypes::CValue(new types::LongDouble());
}


/* string literal */

void SemanticCheckVisitor::visit(astnodes::StringLiteral * stringLiteral)
{
   stringLiteral->valType = new valuetypes::LValue(new types::ArrayType(new types::UnsignedChar(), stringLiteral->str.length(), 1));
}




/******************************/
/*  3.3.2 Postfix expressions */
/******************************/

/* 3.3.2.1 array subscripting */

void SemanticCheckVisitor::visit(astnodes::ArrayAccessOperator * arrayAccessOperator)
{
    // analyse both sub expressions
    arrayAccessOperator->allChildrenAccept(*this);
    
    // check LHS type
    types::Type* lhsType = arrayAccessOperator->lhsExpr->valType->type;
    
    if (types::IsTypeHelper::isArrayType(lhsType))
    {
        // convert array to pointer type
        lhsType = types::IsTypeHelper::pointerFromArrayType(lhsType);
    }
    else
    {
        // get LtoR state
        if (valuetypes::IsValueTypeHelper::isLValue(arrayAccessOperator->lhsExpr->valType))
        {
            arrayAccessOperator->lhsLtoR = true;
        }
    }
    
    if(!types::IsTypeHelper::isPointerType(lhsType))
    {
        addError(arrayAccessOperator, ERR_CC_ARRAY_ACCESS_NO_POINTER);
        arrayAccessOperator->valType = getInvalidValType();
        return;
    }
    if (!types::IsTypeHelper::isObjectType(types::IsTypeHelper::getPointerType(lhsType)->baseType))
    {
        addError(arrayAccessOperator, ERR_CC_DEREF_INCOMPLETE_TYPE);
        arrayAccessOperator->valType = getInvalidValType();
        return;
    }
    
    arrayAccessOperator->pointerSize = types::IsTypeHelper::getPointerType(lhsType)->baseType->getWordSize();
    
    // check RHS type
    types::Type* rhsType = arrayAccessOperator->rhsExpr->valType->type;
    
    // get LtoR state
    if (valuetypes::IsValueTypeHelper::isLValue(arrayAccessOperator->rhsExpr->valType))
    {
        arrayAccessOperator->rhsLtoR = true;
    }
    
    if(!types::IsTypeHelper::isIntegralType(rhsType))
    {
        addError(arrayAccessOperator, ERR_CC_ARRAY_SUB_NOT_INT);
        arrayAccessOperator->valType = getInvalidValType();
        return;
    }
    
    arrayAccessOperator->valType = new valuetypes::LValue(types::IsTypeHelper::getPointerType(lhsType)->baseType);
}


/* 3.3.2.2 Function calls */

void SemanticCheckVisitor::visit(astnodes::MethodCall * methodCall)
{
    // analyse lhs and all arguments
    methodCall->allChildrenAccept(*this);
    
    // check LHS type
    types::Type* lhsType = methodCall->lhsExpr->valType->type;
    
    types::FunctionType* funType = NULL;
    
    if (! valuetypes::IsValueTypeHelper::isFunctionDesignator(methodCall->lhsExpr->valType))
    {
        if (!types::IsTypeHelper::isFunctionType(lhsType)){
            if(!types::IsTypeHelper::isPointerType(lhsType))
            {
                addError(methodCall, ERR_CC_CALLED_OBJ_NOT_FUNC);
                methodCall->valType = getInvalidValType();
                return;
            }
            if (!types::IsTypeHelper::isFunctionType(types::IsTypeHelper::getPointerType(lhsType)->baseType))
            {
                addError(methodCall, ERR_CC_CALLED_OBJ_NOT_FUNC);
                methodCall->valType = getInvalidValType();
                return;
            }
            funType = types::IsTypeHelper::getFunctionType(types::IsTypeHelper::getPointerType(lhsType)->baseType);
        }
        else
        {
            funType = types::IsTypeHelper::getFunctionType(lhsType);
        }
    }
    else
    {
        funType = types::IsTypeHelper::getFunctionType(methodCall->lhsExpr->valType->type);
    }
    
    // now we are sure it is a pointer to a function, get the functiontype
    methodCall->valType = new valuetypes::RValue(funType->returnType);
    
    // check for same size (or in case of variable arguments (...) for the call having
    // at least as many arguments as there are parameters in the function type.
    if ((funType->isVarArgs && methodCall->rhsExprs->size() < funType->paramTypes->size())
        || (!funType->isVarArgs && methodCall->rhsExprs->size() != funType->paramTypes->size()))
    {
        addError(methodCall, ERR_CC_CALLED_FUNC_NUM_PARAMS);
        return;
    }
    
    for (unsigned int i = 0; i < methodCall->rhsExprs->size(); i++)
    {
        valuetypes::ValueType* from = (*methodCall->rhsExprs)[i]->valType;
        
        
        if (i >= funType->paramTypes->size())
        {
            methodCall->varArgsSize += from->type->getWordSize();
        }
        else
        {
            types::Type* to = (*funType->paramTypes)[i];
            // make sure all the parameter types match
            // TODO check that they are assignable (see assignment operator)
            // TODO FIXME TODO FIXME
        }
    }
    
    // get return type
    
    methodCall->returnType = funType->returnType;
}


/* 3.3.2.3 Structure and union members */

void SemanticCheckVisitor::visit(astnodes::StructureResolutionOperator * structureResolutionOperator)
{
    // TODO FIXME TODO FIXME
    // TODO implement structs properly
    printAstName("StructureResolutionOperator");
    structureResolutionOperator->allChildrenAccept(*this);
}


/* 3.3.2.4 Postfix increment and decrement operators */

void SemanticCheckVisitor::visit(astnodes::PostIncDec * postIncDec)
{
    // fist analyse the inner expression
    postIncDec->allChildrenAccept(*this);
    
    // check that the expression type is a scalar type
    if(!types::IsTypeHelper::isScalarType(postIncDec->expr->valType->type))
    {
        addError(postIncDec, ERR_CC_EXPECTED_SCALAR_INCDEC);
    }
    
    // if this is a pointer operation, set the size of the pointer
    if (types::IsTypeHelper::isPointerType(postIncDec->expr->valType->type))
    {
        postIncDec->pointerSize = types::IsTypeHelper::getPointerBaseSize(postIncDec->expr->valType->type);
    }
    
    // check that the expression type is a scalar type
    if(!valuetypes::IsValueTypeHelper::isModifiableLValue(postIncDec->expr->valType))
    {
        addError(postIncDec, ERR_CC_INCDEC_NO_MOD_LVALUE);
    }
    
    // same value type
    postIncDec->valType = valuetypes::IsValueTypeHelper::toRValue(postIncDec->expr->valType);
}



/******************************/
/*  3.3.3 Unary operators     */
/******************************/

/* 3.3.3.1 Prefix increment and decrement operators */

void SemanticCheckVisitor::visit(astnodes::PreIncDec * preIncDec)
{
    // fist analyse the inner expression
    preIncDec->allChildrenAccept(*this);
    
    // check that the expression type is a scalar type
    if(!types::IsTypeHelper::isScalarType(preIncDec->expr->valType->type))
    {
        addError(preIncDec, ERR_CC_EXPECTED_SCALAR_INCDEC);
    }
    
    // if this is a pointer operation, set the size of the pointer
    if (types::IsTypeHelper::isPointerType(preIncDec->expr->valType->type))
    {
        preIncDec->pointerSize = types::IsTypeHelper::getPointerBaseSize(preIncDec->expr->valType->type);
    }
    
    // check that the expression type is a scalar type
    if(!valuetypes::IsValueTypeHelper::isModifiableLValue(preIncDec->expr->valType))
    {
        addError(preIncDec, ERR_CC_INCDEC_NO_MOD_LVALUE);
    }
    
    // same value type
    preIncDec->valType = valuetypes::IsValueTypeHelper::toRValue(preIncDec->expr->valType);
}


/* 3.3.3.2 Address and indirection operators */

void SemanticCheckVisitor::visit(astnodes::AddressOfOperator * addressOfOperator)
{
    // analyse inner expression first:
    addressOfOperator->allChildrenAccept(*this);
    
    valuetypes::ValueType* vtype = addressOfOperator->expr->valType;
    types::Type* type = vtype->type;
    
    if (!valuetypes::IsValueTypeHelper::isLValue(vtype))
    {
        addError(addressOfOperator, ERR_CC_ADRESS_OF_NON_LVALUE);
        addressOfOperator->valType = getInvalidValType();
        return;
    }
    if (types::IsTypeHelper::isArrayType(type))
    {
        addressOfOperator->valType = valuetypes::IsValueTypeHelper::toCorRValue(types::IsTypeHelper::pointerFromArrayType(type) , vtype);
    }
    else
    {
        addressOfOperator->valType = valuetypes::IsValueTypeHelper::toCorRValue(new types::PointerType(type), vtype);
    }
}

void SemanticCheckVisitor::visit(astnodes::DerefOperator * derefOperator)
{
    // analyse inner expression first:
    derefOperator->allChildrenAccept(*this);
    
    valuetypes::ValueType* vtype = derefOperator->expr->valType;
    types::Type* type = vtype->type;
    
    if (valuetypes::IsValueTypeHelper::isLValue(vtype))
    {
        if (types::IsTypeHelper::isArrayType(type))
            type = types::IsTypeHelper::pointerFromArrayType(type);
        else
            derefOperator->LtoR = true;
    }
    
    if (!types::IsTypeHelper::isPointerType(type))
    {
        addError(derefOperator, ERR_CC_DEREF_NON_POINTER);
        derefOperator->valType = getInvalidValType();
        return;
    }
    types::PointerType* ptrType = types::IsTypeHelper::getPointerType(type);
    types::Type* baseType = ptrType->baseType;
    
    derefOperator->valType = new valuetypes::LValue(baseType);
}


/* 3.3.3.3 Unary arithmetic operators */

void SemanticCheckVisitor::visit(astnodes::UnaryOperator * unaryOperator)
{
    // analyse inner expression first:
    unaryOperator->allChildrenAccept(*this);
    
    valuetypes::ValueType* vtype = unaryOperator->expr->valType;
    types::Type* type = vtype->type;
    
    if (valuetypes::IsValueTypeHelper::isLValue(vtype))
    {
        
        if (types::IsTypeHelper::isArrayType(type))
            type = types::IsTypeHelper::pointerFromArrayType(type);
        else
            unaryOperator->LtoR = true;
    }
    
    switch(unaryOperator->optoken)
    {
        case ADD_OP:
        case SUB_OP:
            // check that the expression type is a arithmetic type
            if(!types::IsTypeHelper::isArithmeticType(type))
            {
                addError(unaryOperator, ERR_CC_UNARY_PLUS_MINUS_ARITH);
                unaryOperator->valType = getInvalidValType();
                return;
            }
            unaryOperator->valType = valuetypes::PromotionHelper::promote(vtype);
            break;
            
        case BIN_INV_OP:
            // check that the expression type is a arithmetic type
            if(!types::IsTypeHelper::isIntegralType(type))
            {
                addError(unaryOperator, ERR_CC_UNARY_INV_INTEGRAL);
                unaryOperator->valType = getInvalidValType();
                return;
            }
            unaryOperator->valType = valuetypes::PromotionHelper::promote(vtype);
            break;
            
        case NOT_OP:
            // check that the expression type is a arithmetic type
            if(!types::IsTypeHelper::isScalarType(type))
            {
                addError(unaryOperator, ERR_CC_UNARY_NOT_SCALAR);
                unaryOperator->valType = getInvalidValType();
                return;
            }
            unaryOperator->valType = new valuetypes::RValue(new types::SignedInt());
            break;
        default:
            throw new errors::InternalCompilerException("Unknown unary operator encountered");
    }
}


/* 3.3.3.4 The sizeof operator */

void SemanticCheckVisitor::visit(astnodes::SizeOfOperator * sizeOfOperator)
{
    // analyse the expression inside the sizeof operator
    sizeOfOperator->allChildrenAccept(*this);
    
    // set return type as a CValue (constant RValue)
    sizeOfOperator->valType = new valuetypes::CValue(new types::UnsignedInt());
    
    types::Type* type = NULL;
    if (sizeOfOperator->expr != NULL)
    {
        // get the size of the expression type 
        // check it is not a function type
        if (valuetypes::IsValueTypeHelper::isFunctionDesignator(sizeOfOperator->expr->valType))
        {
            addError(sizeOfOperator, ERR_CC_SIZEOF_FUNC);
            return;
        }
        type = sizeOfOperator->expr->valType->type;
    }
    else if (sizeOfOperator->typeName != NULL)
    {
        type = sizeOfOperator->typeName->type;
    }
    else
    {
        throw new errors::InternalCompilerException("invalid sizeof operator");
    }
    
    if (!type->isComplete())
    {
        addError(sizeOfOperator, ERR_CC_SIZEOF_INCOMPLETE);
        return;
    }
    
    // return the word size of the type
    sizeOfOperator->constExpr = new astnodes::UnsignedIntLiteral(type->getByteSize());
    sizeOfOperator->constExpr->accept(*this);
    
    // set val type
    sizeOfOperator->valType = sizeOfOperator->constExpr->valType;
}


/******************************/
/*  3.3.4 Cast operators      */
/******************************/

void SemanticCheckVisitor::visit(astnodes::ExplicitCastOperator * explicitCastOperator)
{
    // analyse the typename and the expression:
    explicitCastOperator->allChildrenAccept(*this);
    
    // get the type
    types::Type* fromType = explicitCastOperator->expr->valType->type;
    types::Type* toType = explicitCastOperator->typeName->type;
    
    if (valuetypes::IsValueTypeHelper::isLValue(explicitCastOperator->expr->valType))
    {
        
        if (types::IsTypeHelper::isArrayType(fromType))
            fromType = types::IsTypeHelper::pointerFromArrayType(fromType);
        else
            explicitCastOperator->LtoR = true;
    }
    
    // check that these types are castable to each other
    if (types::IsTypeHelper::isArithmeticType(fromType) && types::IsTypeHelper::isArithmeticType(toType))   
    {
        // L to R will be done by the TypeConversionOperator
        explicitCastOperator->LtoR = false;
        
        // insert a type conversion operator
        explicitCastOperator->expr = new astnodes::TypeConversionOperator(explicitCastOperator->expr, fromType, toType);
        
        // visit the type conversion operator (which is not recursive)
        // but needs to set up a few things
        explicitCastOperator->expr->accept(*this);
        
        // take over the valType of the type conversion operator
        explicitCastOperator->valType = explicitCastOperator->expr->valType;
    }
    else if (types::IsTypeHelper::isPointerType(fromType) && types::IsTypeHelper::isPointerType(toType))
    {
        explicitCastOperator->valType = new valuetypes::RValue(toType);
    }
    else if (types::IsTypeHelper::isArithmeticType(fromType) && types::IsTypeHelper::isPointerType(toType))
    {
        // L to R will be done by the TypeConversionOperator
        explicitCastOperator->LtoR = false;
        
        // insert a type conversion operator to convert to uint16_t
        explicitCastOperator->expr = new astnodes::TypeConversionOperator(explicitCastOperator->expr, fromType, new types::UnsignedInt());
        
        // visit the type conversion operator (which is not recursive)
        // but needs to set up a few things
        explicitCastOperator->expr->accept(*this);
        
        // set the wanted pointer type
        explicitCastOperator->valType = new valuetypes::RValue(toType);
    }
    else if (types::IsTypeHelper::isPointerType(fromType) && types::IsTypeHelper::isArithmeticType(toType))
    {
        // L to R will be done by the TypeConversionOperator
        explicitCastOperator->LtoR = false;
        
        // insert a type conversion operator to convert from uint to the arithmetic type
        explicitCastOperator->expr = new astnodes::TypeConversionOperator(explicitCastOperator->expr, new types::UnsignedInt(), toType);
        
        // visit the type conversion operator (which is not recursive)
        // but needs to set up a few things
        explicitCastOperator->expr->accept(*this);
        
        // set the wanted pointer type
        explicitCastOperator->valType = new valuetypes::RValue(toType);
    }
    else
    {
        
        addError(explicitCastOperator, ERR_CC_INVALID_EXPLICIT_CAST);
        explicitCastOperator->valType = getInvalidValType();
        return;
    }
}



/******************************/
/*     binary operators       */
/******************************/

void SemanticCheckVisitor::visit(astnodes::BinaryOperator * binaryOperator)
{
    // analyse both sub-expressions
    binaryOperator->allChildrenAccept(*this);
    
    valuetypes::ValueType* lhsVtype = binaryOperator->lhsExrp->valType;
    valuetypes::ValueType* rhsVtype = binaryOperator->rhsExpr->valType;
    types::Type* lhsType = lhsVtype->type;
    types::Type* rhsType = rhsVtype->type;
    
    /* check for LValue to RValue conversions */
    if (valuetypes::IsValueTypeHelper::isLValue(lhsVtype))
    {
        
        if (types::IsTypeHelper::isArrayType(lhsType))
            lhsType = types::IsTypeHelper::pointerFromArrayType(lhsType);
        else
            binaryOperator->lhsLtoR = true;
    }
    if (valuetypes::IsValueTypeHelper::isLValue(rhsVtype))
    {
        if (types::IsTypeHelper::isArrayType(rhsType))
            rhsType = types::IsTypeHelper::pointerFromArrayType(rhsType);
        else
            binaryOperator->rhsLtoR = true;
    }
    
    
    switch(binaryOperator->optoken)
    {
        
        /* 3.3.5 Multiplicative operators */
        
        case MUL_OP:
        case DIV_OP:
            // check that the expression type is a arithmetic type
            if((!types::IsTypeHelper::isArithmeticType(lhsType))
                || !types::IsTypeHelper::isArithmeticType(rhsType))
            {
                addError(binaryOperator, ERR_CC_BIN_EXPECTED_ARITH);
                binaryOperator->valType = getInvalidValType();
                return;
            }
            binaryOperator->valType = valuetypes::PromotionHelper::commonType(lhsVtype, rhsVtype);
            binaryOperator->commonType = binaryOperator->valType->type;
            break;
        case MOD_OP:
            // check that the expression type is a integral type
            if((!types::IsTypeHelper::isIntegralType(lhsType))
                || !types::IsTypeHelper::isIntegralType(rhsType))
            {
                addError(binaryOperator, ERR_CC_BIN_EXPECTED_INTEGRAL);
                binaryOperator->valType = getInvalidValType();
                return;
            }
            binaryOperator->valType = valuetypes::PromotionHelper::commonType(lhsVtype, rhsVtype);
            binaryOperator->commonType = binaryOperator->valType->type;
            break;
        
            
        /* 3.3.6 Additive operators */
        
        case ADD_OP:
            if((types::IsTypeHelper::isArithmeticType(lhsType))
                && types::IsTypeHelper::isArithmeticType(rhsType))
            {
                // both are arithmetic types
                // promote:
                binaryOperator->valType = valuetypes::PromotionHelper::commonType(lhsVtype, rhsVtype);
                binaryOperator->commonType = binaryOperator->valType->type;
            }
            else if(((types::IsTypeHelper::isPointerType(lhsType))
                && types::IsTypeHelper::isIntegralType(rhsType)))

            {
                // pointer op
                binaryOperator->rhsPtr = true;
                binaryOperator->pointerSize = types::IsTypeHelper::getPointerBaseSize(lhsType);
                binaryOperator->commonType = types::IntegralPromotion::promote(rhsType);
                binaryOperator->valType = valuetypes::IsValueTypeHelper::toCorRValue(lhsType, lhsVtype, rhsVtype);
            }
            else if ((types::IsTypeHelper::isIntegralType(lhsType))
                && types::IsTypeHelper::isPointerType(rhsType))
            {
                // pointer op
                binaryOperator->rhsPtr = true;
                binaryOperator->pointerSize = types::IsTypeHelper::getPointerBaseSize(rhsType);
                binaryOperator->commonType = types::IntegralPromotion::promote(lhsType);
                binaryOperator->valType = valuetypes::IsValueTypeHelper::toCorRValue(rhsType, lhsVtype, rhsVtype);
            }
            else
            {
                addError(binaryOperator, ERR_CC_BIN_ADD_INVALID_TYPES);
                binaryOperator->valType = getInvalidValType();
                return;
            }
            break;
        case SUB_OP:
            if((types::IsTypeHelper::isArithmeticType(lhsType))
                && types::IsTypeHelper::isArithmeticType(rhsType))
            {
                // both are arithmetic types
                // promote:
                binaryOperator->valType = valuetypes::PromotionHelper::commonType(lhsVtype, rhsVtype);
                binaryOperator->commonType = binaryOperator->valType->type;
            } else if((binaryOperator->optoken == SUB_OP)
                && (types::IsTypeHelper::isPointerType(lhsType))
                && types::IsTypeHelper::isPointerType(rhsType))
            {
                // TODO add ptrdiff_t to stddef.h
                binaryOperator->lhsPtr = true;
                binaryOperator->rhsPtr = true;
                binaryOperator->pointerSize = types::IsTypeHelper::getPointerBaseSize(lhsType);
                binaryOperator->commonType = new types::UnsignedInt();
                // TODO properly check for compatible types pointed to
                if (binaryOperator->pointerSize != types::IsTypeHelper::getPointerBaseSize(rhsType))
                {
                    addError(binaryOperator, ERR_CC_PTR_NOT_COMPAT);
                    binaryOperator->valType = getInvalidValType();
                    return;
                }
                binaryOperator->valType = valuetypes::IsValueTypeHelper::toCorRValue(new types::SignedInt(), lhsVtype, rhsVtype);
            } else if((types::IsTypeHelper::isPointerType(lhsType))
                && types::IsTypeHelper::isIntegralType(rhsType))
            {
                binaryOperator->lhsPtr = true;
                binaryOperator->pointerSize = types::IsTypeHelper::getPointerBaseSize(lhsType);
                binaryOperator->commonType = new types::UnsignedInt();
                binaryOperator->valType = valuetypes::IsValueTypeHelper::toCorRValue(lhsType, lhsVtype, rhsVtype);
            }
            else
            {
                addError(binaryOperator, ERR_CC_BIN_SUB_INVALID_TYPES);
                binaryOperator->valType = getInvalidValType();
                return;
            }
            break;
            
            
        /* 3.3.7 Bitwise shift operators */
            
        case LEFT_OP:
        case RIGHT_OP:
            // check that the expression type is a integral type
            // TODO check that RHS is size 1 otherwise convert!!
            // TODO (same for the pointer ops above)
            if((!types::IsTypeHelper::isIntegralType(lhsType))
                || !types::IsTypeHelper::isIntegralType(rhsType))
            {
                addError(binaryOperator, ERR_CC_BIN_EXPECTED_INTEGRAL);
                binaryOperator->valType = getInvalidValType();
                return;
            }
            
            binaryOperator->commonType = valuetypes::PromotionHelper::promote(lhsVtype)->type;
            binaryOperator->valType = valuetypes::IsValueTypeHelper::toCorRValue(binaryOperator->commonType, lhsVtype, rhsVtype);
            break;
            
        /* 3.3.8 Relational operators */
        case LT_OP:
        case GT_OP:
        case LE_OP:
        case GE_OP:
            if((types::IsTypeHelper::isArithmeticType(lhsType))
                && types::IsTypeHelper::isArithmeticType(rhsType))
            {
                // both are arithmetic types
                // promote:
                binaryOperator->commonType = valuetypes::PromotionHelper::commonType(lhsVtype, rhsVtype)->type;
                binaryOperator->valType = valuetypes::IsValueTypeHelper::toCorRValue(new types::SignedInt(), lhsVtype, rhsVtype);
            } else if((types::IsTypeHelper::isPointerType(lhsType))
                && types::IsTypeHelper::isPointerType(rhsType))
            {
                binaryOperator->lhsPtr = true;
                binaryOperator->rhsPtr = true;
                binaryOperator->pointerSize = types::IsTypeHelper::getPointerBaseSize(lhsType);
                
                // TODO properly check for compatible types pointed to
                if (binaryOperator->pointerSize != types::IsTypeHelper::getPointerBaseSize(rhsType))
                {
                    addError(binaryOperator, ERR_CC_PTR_NOT_COMPAT);
                    binaryOperator->valType = getInvalidValType();
                    return;
                }
                
                binaryOperator->commonType = new types::UnsignedInt();
                binaryOperator->valType = valuetypes::IsValueTypeHelper::toCorRValue(new types::SignedInt(), lhsVtype, rhsVtype);
            }
            else
            {
                addError(binaryOperator, ERR_CC_COMP_INVALID_TYPES);
                binaryOperator->valType = getInvalidValType();
                return;
            }
            break;
            
        
        /* 3.3.9 Equality operators */
        
        case EQ_OP:
        case NE_OP:
            if((types::IsTypeHelper::isArithmeticType(lhsType))
                && types::IsTypeHelper::isArithmeticType(rhsType))
            {
                // both are arithmetic types
                // promote:
                binaryOperator->commonType = valuetypes::PromotionHelper::commonType(lhsVtype, rhsVtype)->type;
                binaryOperator->valType = valuetypes::IsValueTypeHelper::toCorRValue(new types::SignedInt(), lhsVtype, rhsVtype);
            } else if((types::IsTypeHelper::isPointerType(lhsType))
                && types::IsTypeHelper::isPointerType(rhsType))
            {
                binaryOperator->lhsPtr = true;
                binaryOperator->rhsPtr = true;
                binaryOperator->pointerSize = types::IsTypeHelper::getPointerBaseSize(lhsType);
                
                // TODO properly check for compatible types pointed to
                if (binaryOperator->pointerSize != types::IsTypeHelper::getPointerBaseSize(rhsType))
                {
                    addError(binaryOperator, ERR_CC_PTR_NOT_COMPAT);
                    binaryOperator->valType = getInvalidValType();
                    return;
                }
                binaryOperator->commonType = new types::UnsignedInt();
                binaryOperator->valType = valuetypes::IsValueTypeHelper::toCorRValue(new types::SignedInt(), lhsVtype, rhsVtype);
            }
            // TODO case for Null pointer constant
            else
            {
                addError(binaryOperator, ERR_CC_COMP_INVALID_TYPES);
                binaryOperator->valType = getInvalidValType();
                return;
            }
            break;
        
            
        /* 3.3.10 Bitwise AND operator */
        /* 3.3.11 Bitwise exclusive OR operator */
        /* 3.3.12 Bitwise inclusive OR operator */
        
        case BIN_AND_OP:
        case BIN_XOR_OP:
        case BIN_OR_OP:
            // check that the expression type is a integral type
            if((!types::IsTypeHelper::isIntegralType(lhsType))
                || !types::IsTypeHelper::isIntegralType(rhsType))
            {
                addError(binaryOperator, ERR_CC_BIN_EXPECTED_INTEGRAL);
                binaryOperator->valType = getInvalidValType();
                return;
            }
            
            binaryOperator->valType = valuetypes::PromotionHelper::commonType(lhsVtype, rhsVtype);
            binaryOperator->commonType = binaryOperator->valType->type;
            break;
            
            
            /* 3.3.13 Logical AND operator */
            /* 3.3.14 Logical OR operator */
            
        case AND_OP:
        case OR_OP:
            
            // check that the expression type is a integral type
            if((!types::IsTypeHelper::isIntegralType(lhsType))
                || !types::IsTypeHelper::isIntegralType(rhsType))
            {
                addError(binaryOperator, ERR_CC_BIN_EXPECTED_INTEGRAL);
                binaryOperator->valType = getInvalidValType();
                return;
            }

            binaryOperator->commonType = valuetypes::PromotionHelper::commonType(lhsVtype, rhsVtype)->type;
            binaryOperator->valType = valuetypes::IsValueTypeHelper::toCorRValue(new types::SignedInt(), lhsVtype, rhsVtype);
            break;
            
        default:
            throw new errors::InternalCompilerException("unknown binary operator encountered");
    }
}

/********************************/
/* 3.3.15 Conditional operator  */
/********************************/
 
void SemanticCheckVisitor::visit(astnodes::ConditionalOperator * conditionalOperator)
{
    // TODO TODO TODO TODO
    // TODO TODO TODO TODO
    // TODO TODO TODO TODO
    printAstName("ConditionalOperator");
    conditionalOperator->allChildrenAccept(*this);
}



/********************************/
/* 3.3.16 Assignment operators  */
/********************************/

void SemanticCheckVisitor::visit(astnodes::AssignmentOperator * assignmentOperator)
{
    // first check lhs and rhs expression
    assignmentOperator->allChildrenAccept(*this);
    
    valuetypes::ValueType* lhsVtype = assignmentOperator->lhsExrp->valType;
    valuetypes::ValueType* rhsVtype = assignmentOperator->rhsExpr->valType;
    types::Type* lhsType = lhsVtype->type;
    types::Type* rhsType = rhsVtype->type;
   
    
    // check that the lhs is a modifiable LValue
    if(!valuetypes::IsValueTypeHelper::isModifiableLValue(assignmentOperator->lhsExrp->valType))
    {
        addError(assignmentOperator, ERR_CC_ASSIGN_NO_MOD_LVALUE);
    }
    
    /* check for LtoR conversion */
    if (valuetypes::IsValueTypeHelper::isLValue(rhsVtype))
    {
        if (types::IsTypeHelper::isArrayType(rhsType))
            rhsType = types::IsTypeHelper::pointerFromArrayType(rhsType);
        else
            assignmentOperator->rhsLtoR = true;
    }
    
    /* convert array to pointer type */
    if (types::IsTypeHelper::isArrayType(lhsType))
    {
        lhsType = types::IsTypeHelper::pointerFromArrayType(lhsType);
    }
    
    switch (assignmentOperator->optoken)
    {
        
        /* 3.3.16.1 Simple assignment */
        
        case ASSIGN_EQUAL:
            if((types::IsTypeHelper::isArithmeticType(lhsType))
                && types::IsTypeHelper::isArithmeticType(rhsType))
            {
                // both are arithmetic types
                // promote:
                assignmentOperator->valType = new valuetypes::RValue(lhsType);
                assignmentOperator->commonType = valuetypes::PromotionHelper::commonType(lhsVtype, rhsVtype)->type;
            }
            else if((types::IsTypeHelper::isPointerType(lhsType))
                && types::IsTypeHelper::isPointerType(rhsType))
            {
                // TODO properly check for compatible types pointed to
                assignmentOperator->ptrop = true;
                assignmentOperator->commonType = new types::UnsignedInt();
                assignmentOperator->valType = new valuetypes::RValue(lhsType);
            }
            else if((types::IsTypeHelper::isStructUnionType(lhsType))
                && types::IsTypeHelper::isStructUnionType(rhsType))
            {
                // TODO properly check for compatible structs/unions
                assignmentOperator->commonType = lhsType;
                assignmentOperator->valType = new valuetypes::RValue(lhsType);
            }
            else
            {
                addError(assignmentOperator, ERR_CC_ASSIGN_INVALID_TYPES);
                assignmentOperator->valType = getInvalidValType();
                return;
            }
            break;
            
            
        /* 3.3.16.2 Compound assignment */
            
        case ADD_ASSIGN:
        case SUB_ASSIGN:
            if((types::IsTypeHelper::isArithmeticType(lhsType))
                && types::IsTypeHelper::isArithmeticType(rhsType))
            {
                // both are arithmetic types
                // promote:
                assignmentOperator->valType = new valuetypes::RValue(lhsType);
                assignmentOperator->commonType = valuetypes::PromotionHelper::commonType(lhsVtype, rhsVtype)->type;
            }
            else if(((types::IsTypeHelper::isPointerType(lhsType))
                && types::IsTypeHelper::isIntegralType(rhsType)))
                
            {
                // pointer op
                assignmentOperator->ptrop = true;
                assignmentOperator->commonType = new types::UnsignedInt();
                assignmentOperator->pointerSize = types::IsTypeHelper::getPointerBaseSize(lhsType);
                assignmentOperator->valType = new valuetypes::RValue(lhsType);
            }
            else
            {
                addError(assignmentOperator, ERR_CC_ASSIGN_INVALID_TYPES);
                assignmentOperator->valType = getInvalidValType();
                return;
            }
            break;
            
        case MUL_ASSIGN:
        case DIV_ASSIGN:
            if((types::IsTypeHelper::isArithmeticType(lhsType))
                && types::IsTypeHelper::isArithmeticType(rhsType))
            {
                // both are arithmetic types
                // promote:
                assignmentOperator->valType = new valuetypes::RValue(lhsType);
                assignmentOperator->commonType = valuetypes::PromotionHelper::commonType(lhsVtype, rhsVtype)->type;
            }
            else
            {
                addError(assignmentOperator, ERR_CC_ASSIGN_INVALID_TYPES);
                assignmentOperator->valType = getInvalidValType();
                return;
            }
            break;
            
        case MOD_ASSIGN:
        case LEFT_ASSIGN:
        case RIGHT_ASSIGN:
        case AND_ASSIGN:
        case XOR_ASSIGN:
        case OR_ASSIGN:
            if((types::IsTypeHelper::isIntegralType(lhsType))
                && types::IsTypeHelper::isIntegralType(rhsType))
            {
                // both are arithmetic types
                // promote:
                assignmentOperator->valType = new valuetypes::RValue(lhsType);
                assignmentOperator->commonType = valuetypes::PromotionHelper::commonType(lhsVtype, rhsVtype)->type;
            }
            else
            {
                addError(assignmentOperator, ERR_CC_ASSIGN_INVALID_TYPES);
                assignmentOperator->valType = getInvalidValType();
                return;
            }
            break;
        default:
            throw new errors::InternalCompilerException("unknown assignment operator encountered");
    }
}



/********************************/
/*  3.3.17 Comma operator       */
/********************************/

void SemanticCheckVisitor::visit(astnodes::ChainExpressions * chainExpressions)
{
    // analyse all children
    chainExpressions->allChildrenAccept(*this);
    
    // check if all values types are constant
    bool isCValue = true;
    if (chainExpressions->exprs != NULL && chainExpressions->exprs->size() > 0)
    {
        for (astnodes::Expressions::iterator i = chainExpressions->exprs->begin(); i != chainExpressions->exprs->end(); ++i)
        {
            if (!valuetypes::IsValueTypeHelper::isCValue((*i)->valType))
                isCValue = false;
        }
        
        if (isCValue)
            chainExpressions->valType = new valuetypes::CValue(chainExpressions->exprs->back()->valType->type);
        else
            chainExpressions->valType = new valuetypes::RValue(chainExpressions->exprs->back()->valType->type);
    }
    else
    {
        throw new errors::InternalCompilerException("chain expressions without any expressions");
    }
}






// TODO implement those below


void SemanticCheckVisitor::visit(astnodes::TypeConversionOperator * typeConv)
{
    // ATTENTION: this is not allowed to be recursive.
    if (valuetypes::IsValueTypeHelper::isLValue(typeConv->expr->valType)
        && !types::IsTypeHelper::isArrayType(typeConv->expr->valType->type)
    )
    {
        typeConv->LtoR = true;
    }
    
    typeConv->valType = new valuetypes::RValue(typeConv->toType);
}


void SemanticCheckVisitor::visit(astnodes::Enumerator * enumerator)
{
    printAstName("Enumerator");
    enumerator->allChildrenAccept(*this);
}


void SemanticCheckVisitor::visit(astnodes::AssemblyStatement * assemblyStatement)
{
    printAstName("AssemblyStatement");
    assemblyStatement->allChildrenAccept(*this);
}


void SemanticCheckVisitor::visit(astnodes::BuiltInVaStart * builtInVaStart)
{
    printAstName("BuiltInVaStart");
    builtInVaStart->allChildrenAccept(*this);
}


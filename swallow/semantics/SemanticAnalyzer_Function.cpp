/* SemanticAnalyzer_Function.cpp --
 *
 * Copyright (c) 2014, Lex Chou <lex at chou dot it>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Swallow nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include "SemanticAnalyzer.h"
#include "FunctionSymbol.h"
#include "GenericDefinition.h"
#include "TypeBuilder.h"
#include "FunctionSymbol.h"
#include "FunctionOverloadedSymbol.h"
#include "ast/ast.h"
#include "common/Errors.h"
#include "SymbolScope.h"
#include "SymbolRegistry.h"
#include "GlobalScope.h"
#include "ScopeGuard.h"
#include "ScopedNodes.h"
#include "common/ScopedValue.h"
#include <set>
#include <cassert>

USE_SWALLOW_NS
using namespace std;

void SemanticAnalyzer::visitParameter(const ParameterPtr& node)
{
    TypePtr type = lookupType(node->getDeclaredType());
    node->setType(type);
    //check if local name is already defined
    SymbolScope* scope = symbolRegistry->getCurrentScope();
    SymbolPtr sym = scope->lookup(node->getLocalName());
    if(sym)
    {
        error(node, Errors::E_DEFINITION_CONFLICT, node->getLocalName());
        return;
    }
    if(node->getLocalName() == node->getExternalName())
    {
        warning(node, Errors::W_PARAM_CAN_BE_EXPRESSED_MORE_SUCCINCTLY_1, node->getExternalName());
    }
    //TODO: In-out parameters cannot have default values, and variadic parameters cannot be marked as inout. If you mark a parameter as inout, it cannot also be marked as var or let.

    //Protocols use the same syntax as normal methods, but are not allowed to specify default values for method parameters.
    if(node->getDefaultValue())
    {
        error(node, Errors::E_DEFAULT_ARGUMENT_NOT_PERMITTED_IN_A_PROTOCOL_METHOD);
    }


}
void SemanticAnalyzer::visitParameters(const ParametersPtr& node)
{
    NodeVisitor::visitParameters(node);
    //check variadic parameter, the last parameter cannot be marked as inout parameter
    if(node->isVariadicParameters())
    {
        ParameterPtr param = node->getParameter(node->numParameters() - 1);
        if(param->isInout())
        {
            error(param, Errors::E_INOUT_ARGUMENTS_CANNOT_BE_VARIADIC);
            return;
        }
    }
    std::set<std::wstring> externalNames;
    //check extraneous shorthand external names
    for(const ParameterPtr& param : *node)
    {
        if(!param->getExternalName().empty())
            externalNames.insert(param->getExternalName());
    }
    for(const ParameterPtr& param : *node)
    {
        if(param->isShorthandExternalName())
        {
            if(externalNames.find(param->getLocalName()) != externalNames.end())
            {
                warning(param, Errors::W_EXTRANEOUS_SHARTP_IN_PARAMETER_1, param->getLocalName());
            }
        }
    }
}



TypePtr SemanticAnalyzer::createFunctionType(const std::vector<ParametersPtr>::const_iterator& begin, const std::vector<ParametersPtr>::const_iterator& end, const TypePtr& retType, const GenericDefinitionPtr& generic)
{
    if(begin == end)
        return retType;

    TypePtr returnType = createFunctionType(begin + 1, end, retType, generic);

    std::vector<Type::Parameter> parameterTypes;
    ParametersPtr params = *begin;
    {
        for (const ParameterPtr &param : *params)
        {
            TypePtr paramType = param->getType();
            if(!paramType)
            {
                param->accept(this);
                paramType = param->getType();
                assert(paramType != nullptr);
            }
            assert(paramType != nullptr);
            std::wstring externalName = param->isShorthandExternalName() ? param->getLocalName() : param->getExternalName();
            parameterTypes.push_back(Type::Parameter(externalName, param->isInout(), paramType));
        }
    }

    return Type::newFunction(parameterTypes, returnType, params->isVariadicParameters(), generic);
}

FunctionSymbolPtr SemanticAnalyzer::createFunctionSymbol(const FunctionDefPtr& func, const GenericDefinitionPtr& generic)
{
    std::map<std::wstring, TypePtr> genericTypes;



    TypePtr retType = symbolRegistry->getGlobalScope()->Void();
    if(func->getReturnType())
        retType = lookupType(func->getReturnType());
    TypePtr funcType = createFunctionType(func->getParametersList().begin(), func->getParametersList().end(), retType, generic);
    FunctionSymbolPtr ret(new FunctionSymbol(func->getName(), funcType, func->getBody()));
    return ret;
}
void SemanticAnalyzer::visitClosure(const ClosurePtr& node)
{
    ScopedClosurePtr closure = std::static_pointer_cast<ScopedClosure>(node);

    //check contextual type
    if(t_hint && t_hint->getCategory() != Type::Function)
    {
        error(node, Errors::E_FUNCTION_PROCEDURES_EXPECTD_TYPE_A_DID_YOU_MEAN_TO_CALL_IT_WITH_1, t_hint->toString());
        return;
    }

    TypePtr returnedType = this->lookupType(node->getReturnType());
    std::vector<Type::Parameter> params;
    if(node->getParameters())
    {
        node->getParameters()->accept(this);
        prepareParameters(closure->getScope(), node->getParameters());

        //create a function type for this
        for(const ParameterPtr& param : *node->getParameters())
        {
            TypePtr type = lookupType(param->getDeclaredType());
            assert(type != nullptr);
            const std::wstring& name = param->isShorthandExternalName() ? param->getLocalName() : param->getExternalName();
            params.push_back(Type::Parameter(name, param->isInout(), type));
        }
    }
    if(t_hint)
    {
        node->setType(t_hint);
    }
    else
    {
        TypePtr type = Type::newFunction(params, returnedType, node->getParameters()->isVariadicParameters());
        node->setType(type);
    }

    if(node->getCapture())
    {
        node->getCapture()->accept(this);
    }

    SCOPED_SET(currentFunction, node->getType());

    for(const StatementPtr& st : *node)
    {
        st->accept(this);
    }

}

void SemanticAnalyzer::visitAccessor(const CodeBlockPtr& accessor, const ParametersPtr& params, const SymbolPtr& setter)
{
    if(!accessor)
        return;
    ScopedCodeBlockPtr codeBlock = std::static_pointer_cast<ScopedCodeBlock>(accessor);
    SymbolScope *scope = codeBlock->getScope();
    ScopeGuard scopeGuard(codeBlock.get(), this);
    (void) scopeGuard;

    SymbolPlaceHolderPtr self(new SymbolPlaceHolder(L"self", currentType, SymbolPlaceHolder::R_PARAMETER, SymbolFlagReadable | SymbolFlagInitialized));
    scope->addSymbol(self);
    if(setter)
        scope->addSymbol(setter);

    params->accept(this);
    prepareParameters(scope, params);

    SCOPED_SET(currentFunction, accessor->getType());

    accessor->accept(this);
}
void SemanticAnalyzer::visitSubscript(const SubscriptDefPtr &node)
{
    assert(currentType != nullptr);

    ParametersPtr parameters = node->getParameters();

    // Register subscript as functions to type
    std::vector<ParametersPtr> paramsList = {parameters};
    TypePtr retType = lookupType(node->getReturnType());
    if(!node->getGetter() && !node->getSetter())
        return;

    if(node->getGetter())
    {
        TypePtr funcType = this->createFunctionType(paramsList.begin(), paramsList.end(), retType, nullptr);
        node->getGetter()->setType(funcType);
        FunctionSymbolPtr func(new FunctionSymbol(L"subscript", funcType, node->getGetter()));
        declarationFinished(func->getName(), func, node->getGetter());
    }
    if(node->getSetter())
    {
        //TODO: replace nullptr to void
        TypePtr funcType = this->createFunctionType(paramsList.begin(), paramsList.end(), symbolRegistry->getGlobalScope()->Void(), nullptr);
        node->getSetter()->setType(funcType);
        static_pointer_cast<TypeBuilder>(funcType)->addParameter(Type::Parameter(retType));
        FunctionSymbolPtr func(new FunctionSymbol(L"subscript", funcType, node->getSetter()));
        declarationFinished(func->getName(), func, node->getSetter());
    }

    //TypeInference

    TypePtr type = lookupType(node->getReturnType());
    assert(type != nullptr);
    std::vector<Type::Parameter> params;
    for(const ParameterPtr& param : *node->getParameters())
    {
        wstring name = param->isShorthandExternalName() ? param->getLocalName() : param->getExternalName();
        TypePtr paramType = lookupType(param->getDeclaredType());
        params.push_back(Type::Parameter(name, param->isInout(), paramType));
    }


    //process getter
    visitAccessor(node->getGetter(), parameters, nullptr);
    wstring setterName = L"newValue";
    if(!node->getSetterName().empty())
        setterName = node->getSetterName();
    SymbolPlaceHolderPtr setter(new SymbolPlaceHolder(setterName, retType, SymbolPlaceHolder::R_PARAMETER, SymbolFlagReadable | SymbolFlagInitialized));
    visitAccessor(node->getSetter(), parameters, setter);
    //process setter
}
void SemanticAnalyzer::visitFunction(const FunctionDefPtr& node)
{
    GenericDefinitionPtr generic;
    if(node->getGenericParametersDef())
        generic = prepareGenericTypes(node->getGenericParametersDef());

    //validate function as operator implemention
    if(node->getKind() == FunctionKindOperator)
    {
        int params = node->getParameters(0)->numParameters();
        if(params != 1 && params != 2)
        {
            error(node, Errors::E_OPERATORS_MUST_HAVE_ONE_OR_TWO_ARGUMENTS);
            return;
        }
        int modifiers = node->getModifiers();
        bool isPrefix = modifiers & DeclarationModifiers::Prefix;
        bool isPostfix = modifiers & DeclarationModifiers::Postfix;
        if(isPrefix && params != 1)
        {
            error(node, Errors::E_A_REQUIRES_A_FUNCTION_WITH_ONE_ARGUMENT_1, L"prefix");
            return;
        }
        if(isPostfix && params != 1)
        {
            error(node, Errors::E_A_REQUIRES_A_FUNCTION_WITH_ONE_ARGUMENT_1, L"postfix");
            return;
        }
        if(params == 1 && (!isPrefix && !isPostfix))
        {
            error(node, Errors::E_UNARY_OPERATOR_IMPLEMENTATION_MUST_HAVE_A_PREFIX_OR_POSTFIX_MODIFIER);
            return;
        }
        if(isPostfix && !symbolRegistry->getOperator(node->getName(), OperatorType::PostfixUnary))
        {
            error(node, Errors::E_OPERATOR_IMPLEMENTATION_WITHOUT_MATCHING_OPERATOR_DECLARATION);
            return;
        }
        if(isPrefix && !symbolRegistry->getOperator(node->getName(), OperatorType::PrefixUnary))
        {
            error(node, Errors::E_OPERATOR_IMPLEMENTATION_WITHOUT_MATCHING_OPERATOR_DECLARATION);
            return;
        }
        if(params == 2 && !symbolRegistry->getOperator(node->getName(), OperatorType::InfixBinary))
        {
            error(node, Errors::E_OPERATOR_IMPLEMENTATION_WITHOUT_MATCHING_OPERATOR_DECLARATION);
            return;
        }

    }



    FunctionSymbolPtr func;
    //enter code block's scope
    {
        ScopedCodeBlockPtr codeBlock = std::static_pointer_cast<ScopedCodeBlock>(node->getBody());
        SymbolScope *scope = codeBlock->getScope();

        ScopeGuard scopeGuard(codeBlock.get(), this);
        (void) scopeGuard;

        if(generic)
            generic->registerTo(scope);
        if(currentType && (node->getModifiers() & (DeclarationModifiers::Class | DeclarationModifiers::Static)) == 0)
        {
            SymbolPlaceHolderPtr self(new SymbolPlaceHolder(L"self", currentType, SymbolPlaceHolder::R_PARAMETER, SymbolFlagReadable | SymbolFlagInitialized));
            scope->addSymbol(self);
        }


        for (const ParametersPtr &params : node->getParametersList())
        {
            params->accept(this);
            prepareParameters(scope, params);
        }

        //prepare function's type
        func = createFunctionSymbol(node, generic);
        node->setType(func->getType());
        node->getBody()->setType(func->getType());

        SCOPED_SET(currentFunction, func->getType());

        node->getBody()->accept(this);
        lookupType(node->getReturnType());

        if(node->getModifiers() & DeclarationModifiers::Prefix)
            func->setFlags(SymbolFlagPrefix, true);
        else if(node->getModifiers() & DeclarationModifiers::Postfix)
            func->setFlags(SymbolFlagPostfix, true);
    }


    //check if the same symbol has been defined in this scope
    SymbolScope* scope = symbolRegistry->getCurrentScope();
    SymbolPtr oldSym = scope->lookup(func->getName());
    SymbolPtr sym;
    if(oldSym)
    {
        FunctionOverloadedSymbolPtr overload = std::dynamic_pointer_cast<FunctionOverloadedSymbol>(oldSym);
        if(!overload)
        {
            //wrap it as overload
            if(FunctionSymbolPtr oldFunc = std::dynamic_pointer_cast<FunctionSymbol>(oldSym))
            {
                overload = FunctionOverloadedSymbolPtr(new FunctionOverloadedSymbol(func->getName()));
                overload->add(oldFunc);
                scope->removeSymbol(oldSym);
                scope->addSymbol(overload);
            }
            else
            {
                //error, symbol with same name exists
                error(node, Errors::E_INVALID_REDECLARATION_1, node->getName());
                abort();
            }
        }
        overload->add(func);
        sym = overload;
    }
    else
    {
        scope->addSymbol(func);
        sym = func;
    }
    //put it into type's SymbolMap
    declarationFinished(sym->getName(), func, node);
    /*
    if(TypeDeclaration* declaration = dynamic_cast<TypeDeclaration*>(symbolRegistry->getCurrentScope()->getOwner()))
    {
        if(declaration->getType())
        {
            TypeBuilderPtr type = static_pointer_cast<TypeBuilder>(declaration->getType());
            type->addMember(sym->getName(), func);
        }
    }
*/



}

void SemanticAnalyzer::visitDeinit(const DeinitializerDefPtr& node)
{
    NodeVisitor::visitDeinit(node);
}

void SemanticAnalyzer::visitInit(const InitializerDefPtr& node)
{
    //TypeDeclaration* declaration = dynamic_cast<TypeDeclaration*>(symbolRegistry->getCurrentScope()->getOwner());

    assert(currentType != nullptr);
    TypeBuilderPtr type = static_pointer_cast<TypeBuilder>(currentType);

    ScopedCodeBlockPtr body = std::static_pointer_cast<ScopedCodeBlock>(node->getBody());

    ScopeGuard scopeGuard(body.get(), this);
    (void) scopeGuard;
    node->getParameters()->accept(this);

    prepareParameters(body->getScope(), node->getParameters());
    //prepare implicit self
    SymbolPlaceHolderPtr self(new SymbolPlaceHolder(L"self", currentType, SymbolPlaceHolder::R_PARAMETER, SymbolFlagReadable | SymbolFlagInitialized));
    body->getScope()->addSymbol(self);

    std::vector<Type::Parameter> params;
    for(const ParameterPtr& param : *node->getParameters())
    {
        //init's parameter always has an external name
        //local name will be used if external name is undeclared
        const std::wstring& externalName = param->getExternalName().empty() ? param->getLocalName() : param->getExternalName();
        params.push_back(Type::Parameter(externalName, param->isInout(), param->getType()));
    }

    TypePtr funcType = Type::newFunction(params, type, node->getParameters()->isVariadicParameters());
    funcType->setFlags(SymbolFlagInit, true);

    FunctionSymbolPtr init(new FunctionSymbol(type->getName(), funcType, nullptr));
    declarationFinished(L"init", init, node);

    node->getBody()->setType(funcType);

    SCOPED_SET(currentFunction, funcType);
    node->getBody()->accept(this);
}

void SemanticAnalyzer::visitCodeBlock(const CodeBlockPtr &node)
{
    auto iter = node->begin();
    for(; iter != node->end(); iter++)
    {
        StatementPtr st = *iter;
        if(BinaryOperatorPtr op = dynamic_pointer_cast<BinaryOperator>(st))
            *iter = transformExpression(nullptr, op);
        st->accept(this);
    }
}

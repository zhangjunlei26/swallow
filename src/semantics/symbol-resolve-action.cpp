#include "symbol-resolve-action.h"
#include "symbol-registry.h"
#include "ast/ast.h"
#include "common/compiler_results.h"
#include "swift_errors.h"
#include "symbol-identifier.h"
#include "scoped-nodes.h"
#include "ast/tuple.h"
#include "function-symbol.h"
#include "function-overloaded-symbol.h"
#include <cassert>
#include <set>

USE_SWIFT_NS

SymbolResolveAction::SymbolResolveAction(SymbolRegistry* symbolRegistry, CompilerResults* compilerResults)
    :SemanticNodeVisitor(symbolRegistry, compilerResults)
{

}

void SymbolResolveAction::visitAssignment(const AssignmentPtr& node)
{
    //check node's LHS, it must be a tuple or identifier
    PatternPtr pattern = node->getLHS();
    verifyTuplePattern(pattern);
}
void SymbolResolveAction::verifyTuplePattern(const PatternPtr& pattern)
{

    if(pattern->getNodeType() == NodeType::Identifier)
    {
        IdentifierPtr id = std::static_pointer_cast<Identifier>(pattern);
        TypePtr type = symbolRegistry->lookupType(id->getIdentifier());
        if(type)
        {
            //cannot assign expression to type
            error(id, Errors::E_CANNOT_ASSIGN, id->getIdentifier());
            return;
        }
        NodePtr sym = std::dynamic_pointer_cast<Node>(symbolRegistry->lookupSymbol(id->getIdentifier()));
        if(!sym)
        {
            error(id, Errors::E_USE_OF_UNRESOLVED_IDENTIFIER, id->getIdentifier());
        }
    }
    else if(pattern->getNodeType() == NodeType::Tuple)
    {
        TuplePtr tuple = std::static_pointer_cast<Tuple>(pattern);
        for(const PatternPtr& element : tuple->elements)
        {
            verifyTuplePattern(element);
        }
    }
    else
    {
        error(pattern, Errors::E_CANNOT_ASSIGN);
        return;
    }

}

void SymbolResolveAction::registerPattern(const PatternPtr& pattern)
{
    SymbolScope* scope = symbolRegistry->getCurrentScope();
    if(pattern->getNodeType() == NodeType::Identifier)
    {
        IdentifierPtr id = std::static_pointer_cast<Identifier>(pattern);
        SymbolPtr s = symbolRegistry->lookupSymbol(id->getIdentifier());
        if(s)
        {
            //already defined
            error(id, Errors::E_DEFINITION_CONFLICT, id->getIdentifier());
        }
        else
        {
            scope->addSymbol(std::static_pointer_cast<SymbolIdentifier>(id));
        }
    }
    else if(pattern->getNodeType() == NodeType::Tuple)
    {
        TuplePtr tuple = std::static_pointer_cast<Tuple>(pattern);
        for(const PatternPtr& element : tuple->elements)
        {
            registerPattern(element);
        }
    }
}

void SymbolResolveAction::visitVariables(const VariablesPtr& node)
{
    for(const VariablePtr& var : *node)
    {
        PatternPtr name = var->getName();
        registerPattern(name);
    }
    for(const VariablePtr& v : *node)
    {
        PatternPtr name = v->getName();
        ExpressionPtr initializer = v->getInitializer();
        CodeBlockPtr didSet = v->getDidSet();
        CodeBlockPtr willSet = v->getWillSet();
        CodeBlockPtr getter = v->getGetter();
        CodeBlockPtr setter = v->getSetter();
        setFlag(name, true, Identifier::F_INITIALIZING);
        if(initializer)
        {
            initializer->accept(this);
        }
        setFlag(name, true, Identifier::F_INITIALIZED);
        setFlag(name, false, Identifier::F_INITIALIZING);
        if(getter)
        {
            getter->accept(this);
        }
        if(setter)
        {
            std::wstring name = v->getSetterName().empty() ? L"newValue" : v->getSetterName();
            //TODO: replace the symbol to internal value
            ScopedCodeBlockPtr cb = std::static_pointer_cast<ScopedCodeBlock>(setter);
            cb->getScope()->addSymbol(SymbolPtr(new SymbolPlaceHolder(name, v->getType())));
            cb->accept(this);
        }
        if(willSet)
        {
            std::wstring setter = v->getWillSetSetter().empty() ? L"newValue" : v->getWillSetSetter();
            //TODO: replace the symbol to internal value
            ScopedCodeBlockPtr cb = std::static_pointer_cast<ScopedCodeBlock>(willSet);
            cb->getScope()->addSymbol(SymbolPtr(new SymbolPlaceHolder(setter, v->getType())));
            cb->accept(this);
        }
        if(didSet)
        {
            std::wstring setter = v->getDidSetSetter().empty() ? L"oldValue" : v->getDidSetSetter();
            //TODO: replace the symbol to internal value
            ScopedCodeBlockPtr cb = std::static_pointer_cast<ScopedCodeBlock>(didSet);
            cb->getScope()->addSymbol(SymbolPtr(new SymbolPlaceHolder(setter, v->getType())));
            cb->accept(this);
        }

    }

}

void SymbolResolveAction::visitConstants(const ConstantsPtr& node)
{
    for(const ConstantPtr& c : *node)
    {
        registerPattern(c->name);
    }
    //verify initializer
    for(const ConstantPtr& c : *node)
    {
        setFlag(c->name, true, Identifier::F_INITIALIZING);
        c->initializer->accept(this);
        setFlag(c->name, true, Identifier::F_INITIALIZED);
        setFlag(c->name, false, Identifier::F_INITIALIZING);
    }
}

void SymbolResolveAction::visitIdentifier(const IdentifierPtr& id)
{
    SymbolPtr sym = NULL;
    SymbolScope* scope = NULL;
    symbolRegistry->lookupSymbol(id->getIdentifier(), &scope, &sym);
    if(!sym)
    {
        compilerResults->add(ErrorLevel::Error, *id->getSourceInfo(), Errors::E_USE_OF_UNRESOLVED_IDENTIFIER, id->getIdentifier());
        return;
    }
    if(IdentifierPtr identifier = std::dynamic_pointer_cast<Identifier>(sym))
    {
        if((identifier->flags & Identifier::F_INITIALIZING))
        {
            error(id, Errors::E_USE_OF_INITIALIZING_VARIABLE, id->getIdentifier());
        }
        else if((identifier->flags & Identifier::F_INITIALIZED) == 0)
        {
            error(id, Errors::E_USE_OF_UNINITIALIZED_VARIABLE, id->getIdentifier());
        }
        //check if this identifier is accessed inside a class/protocol/extension/struct/enum but defined not in program
        if(dynamic_cast<TypeDeclaration*>(symbolRegistry->getCurrentScope()->getOwner()))
        {
            if(scope->getOwner()->getNodeType() != NodeType::Program)
            {
                error(id, Errors::E_USE_OF_FUNCTION_LOCAL_INSIDE_TYPE, id->getIdentifier());
            }
        }


    }
}
void SymbolResolveAction::visitEnumCasePattern(const EnumCasePatternPtr& node)
{
    //TODO: check if the enum case is defined
}

void SymbolResolveAction::setFlag(const PatternPtr& pattern, bool add, int flag)
{
    NodeType::T t = pattern->getNodeType();
    if(t == NodeType::Identifier)
    {
        IdentifierPtr id = std::static_pointer_cast<Identifier>(pattern);
        if(add)
            id->flags |= flag;
        else
            id->flags &= ~flag;
    }
    else if(t == NodeType::Tuple)
    {
        TuplePtr tuple = std::static_pointer_cast<Tuple>(pattern);
        for(const PatternPtr& element : tuple->elements)
        {
            setFlag(element, add, flag);
        }
    }

}

void SymbolResolveAction::defineType(const std::shared_ptr<TypeDeclaration>& node, Type::Category category)
{
    TypeIdentifierPtr id = node->getIdentifier();
    SymbolScope* scope = NULL;
    TypePtr type;
    //it's inside the type's scope, so need to access parent scope;
    SymbolScope* currentScope = symbolRegistry->getCurrentScope()->getParentScope();

    //check if this type is already defined
    symbolRegistry->lookupType(id->getName(), &scope, &type);
    if(type && scope == currentScope)
    {
        //invalid redeclaration of type T
        error(node, Errors::E_INVALID_REDECLARATION, id->getName());
        return;
    }
    //check for parent class or protocol
    for(const TypeIdentifierPtr& parent : node->parents)
    {
        //check the parent must be a defined type
        type = symbolRegistry->lookupType(parent->getName());
        if(!type)
        {
            error(node, Errors::E_USE_OF_UNDECLARED_TYPE, parent->getName());
            return;
        }
    }
    //register this type
    type = Type::newType(id->getName(), category, node);
    currentScope->addSymbol(type);
}


void SymbolResolveAction::visitClass(const ClassDefPtr& node)
{
    TypePtr type = Type::newType(node->getIdentifier()->getName(), Type::Class, node);
    node->setType(type);
    defineType(node, Type::Class);
    NodeVisitor::visitClass(node);
}
void SymbolResolveAction::visitStruct(const StructDefPtr& node)
{
    TypePtr type = Type::newType(node->getIdentifier()->getName(), Type::Struct, node);
    type->initializer = FunctionOverloadedSymbolPtr(new FunctionOverloadedSymbol());
    node->setType(type);
    defineType(node, Type::Struct);
    NodeVisitor::visitStruct(node);
    /*
    Rule of initializers for structure:
    1) If no custom initializers, compiler will prepare one or two initializers:
        1.1) A default initializer with no arguments if all let/var fields are defined with a default value
        1.2) A default initializer with all let/var fields as initializer's parameters with the same external name,
            the order of the parameters are the exactly the same as them defined in structure
    2) Compiler will not generate initializers if there's custom initializers
     */
    //TypePtr type = node->getType();
    if(type->getInitializer()->numOverloads() == 0)
    {
        //apply rule 1
        //check all fields
        FunctionOverloadedSymbolPtr initializer(new FunctionOverloadedSymbol());

    }


}
void SymbolResolveAction::visitEnum(const EnumDefPtr& node)
{
    defineType(node, Type::Enum);
    NodeVisitor::visitEnum(node);
}
void SymbolResolveAction::visitProtocol(const ProtocolDefPtr& node)
{
    defineType(node, Type::Protocol);
    NodeVisitor::visitProtocol(node);
}
void SymbolResolveAction::visitExtension(const ExtensionDefPtr& node)
{
    NodeVisitor::visitExtension(node);

}
void SymbolResolveAction::visitParameter(const ParameterPtr& node)
{
    TypePtr type = lookupType(node->getDeclaredType());
    node->setType(type);
    //check if local name is already defined
    SymbolPtr sym = symbolRegistry->getCurrentScope()->lookup(node->getLocalName());
    if(sym)
    {
        error(node, Errors::E_DEFINITION_CONFLICT);
        return;
    }
    if(node->getLocalName() == node->getExternalName())
    {
        warning(node, Errors::W_PARAM_CAN_BE_EXPRESSED_MORE_SUCCINCTLY);
    }

}
void SymbolResolveAction::visitParameters(const ParametersPtr& node)
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
                warning(param, Errors::W_EXTRANEOUS_SHARTP_IN_PARAMETER, param->getLocalName());
            }
        }
    }
}



TypePtr SymbolResolveAction::createFunctionType(const std::vector<ParametersPtr>::const_iterator& begin, const std::vector<ParametersPtr>::const_iterator& end, const TypePtr& retType)
{
    if(begin == end)
        return retType;

    TypePtr returnType = createFunctionType(begin + 1, end, retType);

    std::vector<Type::Parameter> parameterTypes;
    ParametersPtr params = *begin;
    for(const ParameterPtr& param : *params)
    {
        param->accept(this);
        TypePtr paramType = param->getType();
        assert(paramType != nullptr);
        std::wstring externalName = param->isShorthandExternalName() ? param->getLocalName() : param->getExternalName();
        parameterTypes.push_back(Type::Parameter(externalName, param->isInout(), paramType));
    }

    return Type::newFunction(parameterTypes, returnType, params->isVariadicParameters());
}
FunctionSymbolPtr SymbolResolveAction::createFunctionSymbol(const FunctionDefPtr& func)
{
    TypePtr retType = lookupType(func->getReturnType());
    TypePtr funcType = createFunctionType(func->getParametersList().begin(), func->getParametersList().end(), retType);
    FunctionSymbolPtr ret(new FunctionSymbol(func->getName(), funcType, func));
    return ret;
}
void SymbolResolveAction::visitFunction(const FunctionDefPtr& node)
{
    for(const ParametersPtr& params : node->getParametersList())
    {
        params->accept(this);
        prepareParameters(node->getBody(), params);
    }
    node->getBody()->accept(this);


    FunctionSymbolPtr func = createFunctionSymbol(node);
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
                error(node, Errors::E_INVALID_REDECLARATION, node->getName());
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
    if(TypeDeclaration* declaration = dynamic_cast<TypeDeclaration*>(symbolRegistry->getCurrentScope()->getOwner()))
    {
        if(declaration->getType())
        {
            declaration->getType()->getSymbols()[sym->getName()] = sym;
        }
    }




}

void SymbolResolveAction::visitDeinit(const DeinitializerDefPtr& node)
{

}

void SymbolResolveAction::prepareParameters(const CodeBlockPtr& codeBlock, const ParametersPtr& params)
{
    SymbolScope* scope = std::static_pointer_cast<ScopedCodeBlock>(codeBlock)->getScope();
    //check and prepare for parameters

    for(const ParameterPtr& param : *params)
    {
        assert(param->getType() != nullptr);
        SymbolPtr sym = scope->lookup(param->getLocalName());
        if(sym)
        {
            error(param, Errors::E_DEFINITION_CONFLICT, param->getLocalName());
            return;
        }
        sym = SymbolPtr(new SymbolPlaceHolder(param->getLocalName(), param->getType()));
        scope->addSymbol(sym);
    }
}
void SymbolResolveAction::visitInit(const InitializerDefPtr& node)
{
    TypeDeclaration* declaration = dynamic_cast<TypeDeclaration*>(symbolRegistry->getCurrentScope()->getOwner());

    assert(declaration != nullptr);
    TypePtr type = declaration->getType();

    node->getParameters()->accept(this);
    prepareParameters(node->getBody(), node->getParameters());
    std::vector<Type::Parameter> params;
    for(const ParameterPtr& param : *node->getParameters())
    {
        const std::wstring& externalName = param->isShorthandExternalName() ? param->getLocalName() : param->getExternalName();
        params.push_back(Type::Parameter(externalName, param->isInout(), param->getType()));
    }

    TypePtr funcType = Type::newFunction(params, type, node->getParameters()->isVariadicParameters());


    FunctionSymbolPtr init(new FunctionSymbol(type->getName(), funcType, nullptr));
    type->getInitializer()->add(init);



    node->getBody()->accept(this);
}



#ifndef FLOAT_LITERAL_H
#define FLOAT_LITERAL_H
#include "expression.h"
#include <string>

SWIFT_NS_BEGIN

class FloatLiteral : public Expression
{
public:
    FloatLiteral();
public:
    virtual void serialize(std::wostream& out);
    virtual void accept(NodeVisitor* visitor);
public:
    std::wstring valueAsString;
    double value;
};

SWIFT_NS_END



#endif//FLOAT_LITERAL_H

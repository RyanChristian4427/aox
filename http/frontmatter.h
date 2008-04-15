// Copyright Oryx Mail Systems GmbH. All enquiries to info@oryx.com, please.

#ifndef FRONTMATTER_H
#define FRONTMATTER_H

#include "string.h"


class FrontMatter
    : public String
{
public:
    static FrontMatter * styleSheet();
    static FrontMatter * title( const String & );
    static FrontMatter * jsToggles();
    static FrontMatter * jQuery();
    static FrontMatter * script( const String & );
};


#endif

//
// Created by Diego Ahumada on 29-06-26.
//

#ifndef WORMEDIT_FIELD_H
#define WORMEDIT_FIELD_H
#include <cstdint>

struct Field {
    uint32_t resolution;
    uint8_t channelcount;

public:
    Field(uint8_t _channelcount);
};



#endif //WORMEDIT_FIELD_H

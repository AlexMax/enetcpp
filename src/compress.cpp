/**
 @file compress.c
 @brief An adaptive order-2 PPM range coder
*/

#include <string.h>
#include "enetcpp/enetcpp.h"

struct ENetSymbol
{
    /* binary indexed tree of symbols */
    uint8_t value;
    uint8_t count;
    uint16_t under;
    uint16_t left, right;

    /* context defined by this symbol */
    uint16_t symbols;
    uint16_t escapes;
    uint16_t total;
    uint16_t parent;
};

/* adaptation constants tuned aggressively for small packet sizes rather than large file compression */
enum
{
    ENET_RANGE_CODER_TOP = 1 << 24,
    ENET_RANGE_CODER_BOTTOM = 1 << 16,

    ENET_CONTEXT_SYMBOL_DELTA = 3,
    ENET_CONTEXT_SYMBOL_MINIMUM = 1,
    ENET_CONTEXT_ESCAPE_MINIMUM = 1,

    ENET_SUBCONTEXT_ORDER = 2,
    ENET_SUBCONTEXT_SYMBOL_DELTA = 2,
    ENET_SUBCONTEXT_ESCAPE_DELTA = 5
};

/* context exclusion roughly halves compression speed, so disable for now */
#undef ENET_CONTEXT_EXCLUSION

struct ENetRangeCoder
{
    /* only allocate enough symbols for reasonable MTUs, would need to be larger for large file compression */
    ENetSymbol symbols[4096];
};

void *enet_range_coder_create()
{
    ENetRangeCoder *rangeCoder = (ENetRangeCoder *)ENet::enet_malloc(sizeof(ENetRangeCoder));
    if (rangeCoder == NULL)
    {
        return NULL;
    }

    return rangeCoder;
}

void enet_range_coder_destroy(void *context)
{
    ENetRangeCoder *rangeCoder = (ENetRangeCoder *)context;
    if (rangeCoder == NULL)
    {
        return;
    }

    ENet::enet_free(rangeCoder);
}

#define ENET_SYMBOL_CREATE(symbol, value_, count_)                                                                     \
    {                                                                                                                  \
        symbol = &rangeCoder->symbols[nextSymbol++];                                                                   \
        symbol->value = value_;                                                                                        \
        symbol->count = count_;                                                                                        \
        symbol->under = count_;                                                                                        \
        symbol->left = 0;                                                                                              \
        symbol->right = 0;                                                                                             \
        symbol->symbols = 0;                                                                                           \
        symbol->escapes = 0;                                                                                           \
        symbol->total = 0;                                                                                             \
        symbol->parent = 0;                                                                                            \
    }

#define ENET_CONTEXT_CREATE(context, escapes_, minimum)                                                                \
    {                                                                                                                  \
        ENET_SYMBOL_CREATE(context, 0, 0);                                                                             \
        (context)->escapes = escapes_;                                                                                 \
        (context)->total = escapes_ + 256 * minimum;                                                                   \
        (context)->symbols = 0;                                                                                        \
    }

static uint16_t enet_symbol_rescale(ENetSymbol *symbol)
{
    uint16_t total = 0;
    for (;;)
    {
        symbol->count -= symbol->count >> 1;
        symbol->under = symbol->count;
        if (symbol->left)
        {
            symbol->under += enet_symbol_rescale(symbol + symbol->left);
        }
        total += symbol->under;
        if (!symbol->right)
        {
            break;
        }
        symbol += symbol->right;
    }
    return total;
}

#define ENET_CONTEXT_RESCALE(context, minimum)                                                                         \
    {                                                                                                                  \
        (context)->total = (context)->symbols ? enet_symbol_rescale((context) + (context)->symbols) : 0;               \
        (context)->escapes -= (context)->escapes >> 1;                                                                 \
        (context)->total += (context)->escapes + 256 * minimum;                                                        \
    }

#define ENET_RANGE_CODER_OUTPUT(value)                                                                                 \
    {                                                                                                                  \
        if (outData >= outEnd)                                                                                         \
            return 0;                                                                                                  \
        *outData++ = value;                                                                                            \
    }

#define ENET_RANGE_CODER_ENCODE(under, count, total)                                                                   \
    {                                                                                                                  \
        encodeRange /= (total);                                                                                        \
        encodeLow += (under)*encodeRange;                                                                              \
        encodeRange *= (count);                                                                                        \
        for (;;)                                                                                                       \
        {                                                                                                              \
            if ((encodeLow ^ (encodeLow + encodeRange)) >= ENET_RANGE_CODER_TOP)                                       \
            {                                                                                                          \
                if (encodeRange >= ENET_RANGE_CODER_BOTTOM)                                                            \
                    break;                                                                                             \
                encodeRange = -encodeLow & (ENET_RANGE_CODER_BOTTOM - 1);                                              \
            }                                                                                                          \
            ENET_RANGE_CODER_OUTPUT(encodeLow >> 24);                                                                  \
            encodeRange <<= 8;                                                                                         \
            encodeLow <<= 8;                                                                                           \
        }                                                                                                              \
    }

#define ENET_RANGE_CODER_FLUSH                                                                                         \
    {                                                                                                                  \
        while (encodeLow)                                                                                              \
        {                                                                                                              \
            ENET_RANGE_CODER_OUTPUT(encodeLow >> 24);                                                                  \
            encodeLow <<= 8;                                                                                           \
        }                                                                                                              \
    }

#define ENET_RANGE_CODER_FREE_SYMBOLS                                                                                  \
    {                                                                                                                  \
        if (nextSymbol >= sizeof(rangeCoder->symbols) / sizeof(ENetSymbol) - ENET_SUBCONTEXT_ORDER)                    \
        {                                                                                                              \
            nextSymbol = 0;                                                                                            \
            ENET_CONTEXT_CREATE(root, ENET_CONTEXT_ESCAPE_MINIMUM, ENET_CONTEXT_SYMBOL_MINIMUM);                       \
            predicted = 0;                                                                                             \
            order = 0;                                                                                                 \
        }                                                                                                              \
    }

#define ENET_CONTEXT_ENCODE(context, symbol_, value_, under_, count_, update, minimum)                                 \
    {                                                                                                                  \
        under_ = value * minimum;                                                                                      \
        count_ = minimum;                                                                                              \
        if (!(context)->symbols)                                                                                       \
        {                                                                                                              \
            ENET_SYMBOL_CREATE(symbol_, value_, update);                                                               \
            (context)->symbols = symbol_ - (context);                                                                  \
        }                                                                                                              \
        else                                                                                                           \
        {                                                                                                              \
            ENetSymbol *node = (context) + (context)->symbols;                                                         \
            for (;;)                                                                                                   \
            {                                                                                                          \
                if (value_ < node->value)                                                                              \
                {                                                                                                      \
                    node->under += update;                                                                             \
                    if (node->left)                                                                                    \
                    {                                                                                                  \
                        node += node->left;                                                                            \
                        continue;                                                                                      \
                    }                                                                                                  \
                    ENET_SYMBOL_CREATE(symbol_, value_, update);                                                       \
                    node->left = symbol_ - node;                                                                       \
                }                                                                                                      \
                else if (value_ > node->value)                                                                         \
                {                                                                                                      \
                    under_ += node->under;                                                                             \
                    if (node->right)                                                                                   \
                    {                                                                                                  \
                        node += node->right;                                                                           \
                        continue;                                                                                      \
                    }                                                                                                  \
                    ENET_SYMBOL_CREATE(symbol_, value_, update);                                                       \
                    node->right = symbol_ - node;                                                                      \
                }                                                                                                      \
                else                                                                                                   \
                {                                                                                                      \
                    count_ += node->count;                                                                             \
                    under_ += node->under - node->count;                                                               \
                    node->under += update;                                                                             \
                    node->count += update;                                                                             \
                    symbol_ = node;                                                                                    \
                }                                                                                                      \
                break;                                                                                                 \
            }                                                                                                          \
        }                                                                                                              \
    }

#ifdef ENET_CONTEXT_EXCLUSION
static const ENetSymbol emptyContext = {0, 0, 0, 0, 0, 0, 0, 0, 0};

#define ENET_CONTEXT_WALK(context, body)                                                                               \
    {                                                                                                                  \
        const ENetSymbol *node = (context) + (context)->symbols;                                                       \
        const ENetSymbol *stack[256];                                                                                  \
        size_t stackSize = 0;                                                                                          \
        while (node->left)                                                                                             \
        {                                                                                                              \
            stack[stackSize++] = node;                                                                                 \
            node += node->left;                                                                                        \
        }                                                                                                              \
        for (;;)                                                                                                       \
        {                                                                                                              \
            body;                                                                                                      \
            if (node->right)                                                                                           \
            {                                                                                                          \
                node += node->right;                                                                                   \
                while (node->left)                                                                                     \
                {                                                                                                      \
                    stack[stackSize++] = node;                                                                         \
                    node += node->left;                                                                                \
                }                                                                                                      \
            }                                                                                                          \
            else if (stackSize <= 0)                                                                                   \
                break;                                                                                                 \
            else                                                                                                       \
                node = stack[--stackSize];                                                                             \
        }                                                                                                              \
    }

#define ENET_CONTEXT_ENCODE_EXCLUDE(context, value_, under, total, minimum)                                            \
    ENET_CONTEXT_WALK(context, {                                                                                       \
        if (node->value != value_)                                                                                     \
        {                                                                                                              \
            uint16_t parentCount = rangeCoder->symbols[node->parent].count + minimum;                                  \
            if (node->value < value_)                                                                                  \
                under -= parentCount;                                                                                  \
            total -= parentCount;                                                                                      \
        }                                                                                                              \
    })
#endif

size_t enet_range_coder_compress(void *context, const ENetBuffer *inBuffers, size_t inBufferCount, size_t inLimit,
                                 uint8_t *outData, size_t outLimit)
{
    ENetRangeCoder *rangeCoder = (ENetRangeCoder *)context;
    uint8_t *outStart = outData, *outEnd = &outData[outLimit];
    const uint8_t *inData, *inEnd;
    uint32_t encodeLow = 0, encodeRange = ~0;
    ENetSymbol *root;
    uint16_t predicted = 0;
    size_t order = 0, nextSymbol = 0;

    if (rangeCoder == NULL || inBufferCount <= 0 || inLimit <= 0)
    {
        return 0;
    }

    inData = (const uint8_t *)inBuffers->data;
    inEnd = &inData[inBuffers->dataLength];
    inBuffers++;
    inBufferCount--;

    ENET_CONTEXT_CREATE(root, ENET_CONTEXT_ESCAPE_MINIMUM, ENET_CONTEXT_SYMBOL_MINIMUM);

    for (;;)
    {
        ENetSymbol *subcontext, *symbol;
#ifdef ENET_CONTEXT_EXCLUSION
        const ENetSymbol *childContext = &emptyContext;
#endif
        uint8_t value;
        uint16_t count, under, *parent = &predicted, total;
        if (inData >= inEnd)
        {
            if (inBufferCount <= 0)
            {
                break;
            }
            inData = (const uint8_t *)inBuffers->data;
            inEnd = &inData[inBuffers->dataLength];
            inBuffers++;
            inBufferCount--;
        }
        value = *inData++;

        for (subcontext = &rangeCoder->symbols[predicted]; subcontext != root;
#ifdef ENET_CONTEXT_EXCLUSION
             childContext = subcontext,
#endif
            subcontext = &rangeCoder->symbols[subcontext->parent])
        {
            ENET_CONTEXT_ENCODE(subcontext, symbol, value, under, count, ENET_SUBCONTEXT_SYMBOL_DELTA, 0);
            *parent = symbol - rangeCoder->symbols;
            parent = &symbol->parent;
            total = subcontext->total;
#ifdef ENET_CONTEXT_EXCLUSION
            if (childContext->total > ENET_SUBCONTEXT_SYMBOL_DELTA + ENET_SUBCONTEXT_ESCAPE_DELTA)
            {
                ENET_CONTEXT_ENCODE_EXCLUDE(childContext, value, under, total, 0);
            }
#endif
            if (count > 0)
            {
                ENET_RANGE_CODER_ENCODE(subcontext->escapes + under, count, total);
            }
            else
            {
                if (subcontext->escapes > 0 && subcontext->escapes < total)
                {
                    ENET_RANGE_CODER_ENCODE(0, subcontext->escapes, total);
                }
                subcontext->escapes += ENET_SUBCONTEXT_ESCAPE_DELTA;
                subcontext->total += ENET_SUBCONTEXT_ESCAPE_DELTA;
            }
            subcontext->total += ENET_SUBCONTEXT_SYMBOL_DELTA;
            if (count > 0xFF - 2 * ENET_SUBCONTEXT_SYMBOL_DELTA || subcontext->total > ENET_RANGE_CODER_BOTTOM - 0x100)
            {
                ENET_CONTEXT_RESCALE(subcontext, 0);
            }
            if (count > 0)
            {
                goto nextInput;
            }
        }

        ENET_CONTEXT_ENCODE(root, symbol, value, under, count, ENET_CONTEXT_SYMBOL_DELTA, ENET_CONTEXT_SYMBOL_MINIMUM);
        *parent = symbol - rangeCoder->symbols;
        parent = &symbol->parent;
        total = root->total;
#ifdef ENET_CONTEXT_EXCLUSION
        if (childContext->total > ENET_SUBCONTEXT_SYMBOL_DELTA + ENET_SUBCONTEXT_ESCAPE_DELTA)
        {
            ENET_CONTEXT_ENCODE_EXCLUDE(childContext, value, under, total, ENET_CONTEXT_SYMBOL_MINIMUM);
        }
#endif
        ENET_RANGE_CODER_ENCODE(root->escapes + under, count, total);
        root->total += ENET_CONTEXT_SYMBOL_DELTA;
        if (count > 0xFF - 2 * ENET_CONTEXT_SYMBOL_DELTA + ENET_CONTEXT_SYMBOL_MINIMUM ||
            root->total > ENET_RANGE_CODER_BOTTOM - 0x100)
        {
            ENET_CONTEXT_RESCALE(root, ENET_CONTEXT_SYMBOL_MINIMUM);
        }

    nextInput:
        if (order >= ENET_SUBCONTEXT_ORDER)
        {
            predicted = rangeCoder->symbols[predicted].parent;
        }
        else
        {
            order++;
        }
        ENET_RANGE_CODER_FREE_SYMBOLS;
    }

    ENET_RANGE_CODER_FLUSH;

    return (size_t)(outData - outStart);
}

#define ENET_RANGE_CODER_SEED                                                                                          \
    {                                                                                                                  \
        if (inData < inEnd)                                                                                            \
            decodeCode |= *inData++ << 24;                                                                             \
        if (inData < inEnd)                                                                                            \
            decodeCode |= *inData++ << 16;                                                                             \
        if (inData < inEnd)                                                                                            \
            decodeCode |= *inData++ << 8;                                                                              \
        if (inData < inEnd)                                                                                            \
            decodeCode |= *inData++;                                                                                   \
    }

#define ENET_RANGE_CODER_READ(total) ((decodeCode - decodeLow) / (decodeRange /= (total)))

#define ENET_RANGE_CODER_DECODE(under, count, total)                                                                   \
    {                                                                                                                  \
        decodeLow += (under)*decodeRange;                                                                              \
        decodeRange *= (count);                                                                                        \
        for (;;)                                                                                                       \
        {                                                                                                              \
            if ((decodeLow ^ (decodeLow + decodeRange)) >= ENET_RANGE_CODER_TOP)                                       \
            {                                                                                                          \
                if (decodeRange >= ENET_RANGE_CODER_BOTTOM)                                                            \
                    break;                                                                                             \
                decodeRange = -decodeLow & (ENET_RANGE_CODER_BOTTOM - 1);                                              \
            }                                                                                                          \
            decodeCode <<= 8;                                                                                          \
            if (inData < inEnd)                                                                                        \
                decodeCode |= *inData++;                                                                               \
            decodeRange <<= 8;                                                                                         \
            decodeLow <<= 8;                                                                                           \
        }                                                                                                              \
    }

#define ENET_CONTEXT_DECODE(context, symbol_, code, value_, under_, count_, update, minimum, createRoot, visitNode,    \
                            createRight, createLeft)                                                                   \
    {                                                                                                                  \
        under_ = 0;                                                                                                    \
        count_ = minimum;                                                                                              \
        if (!(context)->symbols)                                                                                       \
        {                                                                                                              \
            createRoot;                                                                                                \
        }                                                                                                              \
        else                                                                                                           \
        {                                                                                                              \
            ENetSymbol *node = (context) + (context)->symbols;                                                         \
            for (;;)                                                                                                   \
            {                                                                                                          \
                uint16_t after = under_ + node->under + (node->value + 1) * minimum, before = node->count + minimum;   \
                visitNode;                                                                                             \
                if (code >= after)                                                                                     \
                {                                                                                                      \
                    under_ += node->under;                                                                             \
                    if (node->right)                                                                                   \
                    {                                                                                                  \
                        node += node->right;                                                                           \
                        continue;                                                                                      \
                    }                                                                                                  \
                    createRight;                                                                                       \
                }                                                                                                      \
                else if (code < after - before)                                                                        \
                {                                                                                                      \
                    node->under += update;                                                                             \
                    if (node->left)                                                                                    \
                    {                                                                                                  \
                        node += node->left;                                                                            \
                        continue;                                                                                      \
                    }                                                                                                  \
                    createLeft;                                                                                        \
                }                                                                                                      \
                else                                                                                                   \
                {                                                                                                      \
                    value_ = node->value;                                                                              \
                    count_ += node->count;                                                                             \
                    under_ = after - before;                                                                           \
                    node->under += update;                                                                             \
                    node->count += update;                                                                             \
                    symbol_ = node;                                                                                    \
                }                                                                                                      \
                break;                                                                                                 \
            }                                                                                                          \
        }                                                                                                              \
    }

#define ENET_CONTEXT_TRY_DECODE(context, symbol_, code, value_, under_, count_, update, minimum, exclude)              \
    ENET_CONTEXT_DECODE(context, symbol_, code, value_, under_, count_, update, minimum, return 0,                     \
                        exclude(node->value, after, before), return 0, return 0)

#define ENET_CONTEXT_ROOT_DECODE(context, symbol_, code, value_, under_, count_, update, minimum, exclude)             \
    ENET_CONTEXT_DECODE(                                                                                               \
        context, symbol_, code, value_, under_, count_, update, minimum,                                               \
        {                                                                                                              \
            value_ = code / minimum;                                                                                   \
            under_ = code - code % minimum;                                                                            \
            ENET_SYMBOL_CREATE(symbol_, value_, update);                                                               \
            (context)->symbols = symbol_ - (context);                                                                  \
        },                                                                                                             \
        exclude(node->value, after, before),                                                                           \
        {                                                                                                              \
            value_ = node->value + 1 + (code - after) / minimum;                                                       \
            under_ = code - (code - after) % minimum;                                                                  \
            ENET_SYMBOL_CREATE(symbol_, value_, update);                                                               \
            node->right = symbol_ - node;                                                                              \
        },                                                                                                             \
        {                                                                                                              \
            value_ = node->value - 1 - (after - before - code - 1) / minimum;                                          \
            under_ = code - (after - before - code - 1) % minimum;                                                     \
            ENET_SYMBOL_CREATE(symbol_, value_, update);                                                               \
            node->left = symbol_ - node;                                                                               \
        })

#ifdef ENET_CONTEXT_EXCLUSION
struct ENetExclude
{
    uint8_t value;
    uint16_t under;
};

#define ENET_CONTEXT_DECODE_EXCLUDE(context, total, minimum)                                                           \
    {                                                                                                                  \
        uint16_t under = 0;                                                                                            \
        nextExclude = excludes;                                                                                        \
        ENET_CONTEXT_WALK(context, {                                                                                   \
            under += rangeCoder->symbols[node->parent].count + minimum;                                                \
            nextExclude->value = node->value;                                                                          \
            nextExclude->under = under;                                                                                \
            nextExclude++;                                                                                             \
        });                                                                                                            \
        total -= under;                                                                                                \
    }

#define ENET_CONTEXT_EXCLUDED(value_, after, before)                                                                   \
    {                                                                                                                  \
        size_t low = 0, high = nextExclude - excludes;                                                                 \
        for (;;)                                                                                                       \
        {                                                                                                              \
            size_t mid = (low + high) >> 1;                                                                            \
            const ENetExclude *exclude = &excludes[mid];                                                               \
            if (value_ < exclude->value)                                                                               \
            {                                                                                                          \
                if (low + 1 < high)                                                                                    \
                {                                                                                                      \
                    high = mid;                                                                                        \
                    continue;                                                                                          \
                }                                                                                                      \
                if (exclude > excludes)                                                                                \
                    after -= exclude[-1].under;                                                                        \
            }                                                                                                          \
            else                                                                                                       \
            {                                                                                                          \
                if (value_ > exclude->value)                                                                           \
                {                                                                                                      \
                    if (low + 1 < high)                                                                                \
                    {                                                                                                  \
                        low = mid;                                                                                     \
                        continue;                                                                                      \
                    }                                                                                                  \
                }                                                                                                      \
                else                                                                                                   \
                    before = 0;                                                                                        \
                after -= exclude->under;                                                                               \
            }                                                                                                          \
            break;                                                                                                     \
        }                                                                                                              \
    }
#endif

#define ENET_CONTEXT_NOT_EXCLUDED(value_, after, before)

size_t enet_range_coder_decompress(void *context, const uint8_t *inData, size_t inLimit, uint8_t *outData,
                                   size_t outLimit)
{
    ENetRangeCoder *rangeCoder = (ENetRangeCoder *)context;
    uint8_t *outStart = outData, *outEnd = &outData[outLimit];
    const uint8_t *inEnd = &inData[inLimit];
    uint32_t decodeLow = 0, decodeCode = 0, decodeRange = ~0;
    ENetSymbol *root;
    uint16_t predicted = 0;
    size_t order = 0, nextSymbol = 0;
#ifdef ENET_CONTEXT_EXCLUSION
    ENetExclude excludes[256];
    ENetExclude *nextExclude = excludes;
#endif

    if (rangeCoder == NULL || inLimit <= 0)
    {
        return 0;
    }

    ENET_CONTEXT_CREATE(root, ENET_CONTEXT_ESCAPE_MINIMUM, ENET_CONTEXT_SYMBOL_MINIMUM);

    ENET_RANGE_CODER_SEED;

    for (;;)
    {
        ENetSymbol *subcontext, *symbol, *patch;
#ifdef ENET_CONTEXT_EXCLUSION
        const ENetSymbol *childContext = &emptyContext;
#endif
        uint8_t value = 0;
        uint16_t code, under, count, bottom, *parent = &predicted, total;

        for (subcontext = &rangeCoder->symbols[predicted]; subcontext != root;
#ifdef ENET_CONTEXT_EXCLUSION
             childContext = subcontext,
#endif
            subcontext = &rangeCoder->symbols[subcontext->parent])
        {
            if (subcontext->escapes <= 0)
            {
                continue;
            }
            total = subcontext->total;
#ifdef ENET_CONTEXT_EXCLUSION
            if (childContext->total > 0)
            {
                ENET_CONTEXT_DECODE_EXCLUDE(childContext, total, 0);
            }
#endif
            if (subcontext->escapes >= total)
            {
                continue;
            }
            code = ENET_RANGE_CODER_READ(total);
            if (code < subcontext->escapes)
            {
                ENET_RANGE_CODER_DECODE(0, subcontext->escapes, total);
                continue;
            }
            code -= subcontext->escapes;
#ifdef ENET_CONTEXT_EXCLUSION
            if (childContext->total > 0)
            {
                ENET_CONTEXT_TRY_DECODE(subcontext, symbol, code, value, under, count, ENET_SUBCONTEXT_SYMBOL_DELTA, 0,
                                        ENET_CONTEXT_EXCLUDED);
            }
            else
#endif
            {
                ENET_CONTEXT_TRY_DECODE(subcontext, symbol, code, value, under, count, ENET_SUBCONTEXT_SYMBOL_DELTA, 0,
                                        ENET_CONTEXT_NOT_EXCLUDED);
            }
            bottom = symbol - rangeCoder->symbols;
            ENET_RANGE_CODER_DECODE(subcontext->escapes + under, count, total);
            subcontext->total += ENET_SUBCONTEXT_SYMBOL_DELTA;
            if (count > 0xFF - 2 * ENET_SUBCONTEXT_SYMBOL_DELTA || subcontext->total > ENET_RANGE_CODER_BOTTOM - 0x100)
            {
                ENET_CONTEXT_RESCALE(subcontext, 0);
            }
            goto patchContexts;
        }

        total = root->total;
#ifdef ENET_CONTEXT_EXCLUSION
        if (childContext->total > 0)
        {
            ENET_CONTEXT_DECODE_EXCLUDE(childContext, total, ENET_CONTEXT_SYMBOL_MINIMUM);
        }
#endif
        code = ENET_RANGE_CODER_READ(total);
        if (code < root->escapes)
        {
            ENET_RANGE_CODER_DECODE(0, root->escapes, total);
            break;
        }
        code -= root->escapes;
#ifdef ENET_CONTEXT_EXCLUSION
        if (childContext->total > 0)
        {
            ENET_CONTEXT_ROOT_DECODE(root, symbol, code, value, under, count, ENET_CONTEXT_SYMBOL_DELTA,
                                     ENET_CONTEXT_SYMBOL_MINIMUM, ENET_CONTEXT_EXCLUDED);
        }
        else
#endif
        {
            ENET_CONTEXT_ROOT_DECODE(root, symbol, code, value, under, count, ENET_CONTEXT_SYMBOL_DELTA,
                                     ENET_CONTEXT_SYMBOL_MINIMUM, ENET_CONTEXT_NOT_EXCLUDED);
        }
        bottom = symbol - rangeCoder->symbols;
        ENET_RANGE_CODER_DECODE(root->escapes + under, count, total);
        root->total += ENET_CONTEXT_SYMBOL_DELTA;
        if (count > 0xFF - 2 * ENET_CONTEXT_SYMBOL_DELTA + ENET_CONTEXT_SYMBOL_MINIMUM ||
            root->total > ENET_RANGE_CODER_BOTTOM - 0x100)
        {
            ENET_CONTEXT_RESCALE(root, ENET_CONTEXT_SYMBOL_MINIMUM);
        }

    patchContexts:
        for (patch = &rangeCoder->symbols[predicted]; patch != subcontext; patch = &rangeCoder->symbols[patch->parent])
        {
            ENET_CONTEXT_ENCODE(patch, symbol, value, under, count, ENET_SUBCONTEXT_SYMBOL_DELTA, 0);
            *parent = symbol - rangeCoder->symbols;
            parent = &symbol->parent;
            if (count <= 0)
            {
                patch->escapes += ENET_SUBCONTEXT_ESCAPE_DELTA;
                patch->total += ENET_SUBCONTEXT_ESCAPE_DELTA;
            }
            patch->total += ENET_SUBCONTEXT_SYMBOL_DELTA;
            if (count > 0xFF - 2 * ENET_SUBCONTEXT_SYMBOL_DELTA || patch->total > ENET_RANGE_CODER_BOTTOM - 0x100)
            {
                ENET_CONTEXT_RESCALE(patch, 0);
            }
        }
        *parent = bottom;

        ENET_RANGE_CODER_OUTPUT(value);

        if (order >= ENET_SUBCONTEXT_ORDER)
        {
            predicted = rangeCoder->symbols[predicted].parent;
        }
        else
        {
            order++;
        }
        ENET_RANGE_CODER_FREE_SYMBOLS;
    }

    return (size_t)(outData - outStart);
}

/** @defgroup host ENet host functions
    @{
*/

int enet_host_compress_with_range_coder(ENetHost *host)
{
    ENetCompressor compressor;
    memset(&compressor, 0, sizeof(compressor));
    compressor.context = enet_range_coder_create();
    if (compressor.context == NULL)
    {
        return -1;
    }
    compressor.compress = enet_range_coder_compress;
    compressor.decompress = enet_range_coder_decompress;
    compressor.destroy = enet_range_coder_destroy;
    enet_host_compress(host, &compressor);
    return 0;
}

/** @} */

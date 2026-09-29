//  tuz_enc_match.h
/*
 The MIT License (MIT)
 Copyright (c) 2012-2022 HouSisong All Rights Reserved.
*/
#ifndef _tuz_enc_match_h
#define _tuz_enc_match_h
#include "tuz_sstring.h"
namespace _tuz_private{

    struct TTuzCode;

    typedef TSuffixString::TInt     TInt;
    typedef uint32_t                TUInt;
    typedef TSuffixString::TLCPInt  TLCPInt;
    #if (tuz_kMaxOfDictSize>=(1<<16))
        typedef  TUInt   TPosInt;
    #else
        typedef  TLCPInt TPosInt;
    #endif

    struct TMatch{
        explicit TMatch(const tuz_byte* data,const tuz_byte* data_end,
                        const TTuzCode& _coder,const tuz_TCompressProps& _props)
        :sstring(data,data_end,(TLCPInt)_props.maxSaveLength,_props.threadNum), coder(_coder),
        props(_props),shortEqLenString(-1),shortEqLenCount(0),
        lenStamp(_props.maxSaveLength+1,0),lenStampCur(0){ _initMinDictLenBit(); }
        bool match(const tuz_byte** out_matched,size_t* out_match_len,
                   const tuz_byte* cur);
    private:
        TSuffixString               sstring;
        const TTuzCode&             coder;
        const tuz_TCompressProps&   props;
        std::vector<TPosInt>        dictPos;
        std::vector<TLCPInt>        matchLen;
        std::vector<TLCPInt>        shortEqLen; //cache for short pos match
        TInt                        shortEqLenString;
        size_t                      shortEqLenCount;
        std::vector<TUInt>          lenStamp; //lenStamp[mLen]==lenStampCur: mLen done by a short pos match
        TUInt                       lenStampCur;
        std::vector<TUInt>          minDictLenBit; //min bit count of mLen for a LCP match
        std::vector<TInt>           shortMatchList;
        size_t _updateShortEqLen(const TInt curString,const TInt kShortPos);
        void _initMinDictLenBit();
        void _cost_match(const TInt curString,const size_t curi,
                         size_t* out_maxMatchLen,std::vector<TUInt>& cost);
        void _initCost(std::vector<TUInt>& cost,size_t costSize);
        void _getCost(const tuz_byte* cur0);
        void _getCostByMatch(const tuz_byte* cur0,std::vector<TUInt>& cost);
        void _getCostByLiteralLen(std::vector<TUInt>& cost);
    };
    
}
#endif //_tuz_enc_match_h

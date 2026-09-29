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
        props(_props),shortString(-1),shortGroupCount(0),
        lenStamp(_props.maxSaveLength+1,0),lenStampCur(0){ _initMinDictLenBit(); }
        bool match(const tuz_byte** out_matched,size_t* out_match_len,
                   const tuz_byte* cur);
    private:
        TSuffixString               sstring;
        const TTuzCode&             coder;
        const tuz_TCompressProps&   props;
        std::vector<TPosInt>        dictPos;
        std::vector<TLCPInt>        matchLen;
        struct TShortGroup{
            TInt        eqEnd;
            bool        isCapped;
            uint64_t    mask[2]; //bit dict_pos
        };
        TInt                        shortString; //curString of the short pos match state
        std::vector<TShortGroup>    shortGroups;
        size_t                      shortGroupCount;
        uint64_t                    shortByteMask[256][2]; //bit dict_pos: src[shortByteMaskString-(dict_pos+1)]==byte
        TInt                        shortByteMaskString[256];
        std::vector<TLCPInt>        shortMatchLen;
        TLCPInt                     shortOutLen[128];
        enum { kShortGroupHashSize=256 };
        tuz_byte                    shortGroupHash[kShortGroupHashSize]; //eqEnd -> group index
        std::vector<TUInt>          lenStamp; //lenStamp[mLen]==lenStampCur: mLen done by a short pos match
        TUInt                       lenStampCur;
        std::vector<TUInt>          minDictLenBit; //min bit count of mLen for a LCP match
        std::vector<TInt>           shortMatchList;
        size_t _updateShortMatch(const TInt curString,const TInt kShortPos);
        void _alignByteMask(tuz_byte v,TInt curString);
        void _addShortGroup(TInt eqEnd,bool isCapped,size_t dict_pos);
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

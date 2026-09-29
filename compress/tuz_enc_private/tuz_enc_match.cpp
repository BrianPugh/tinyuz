//  tuz_enc_match.cpp
/*
 The MIT License (MIT)
 Copyright (c) 2012-2022 HouSisong All Rights Reserved.
*/
#include "tuz_enc_match.h"
#include "tuz_enc_code.h"
#include <algorithm> //std::fill
#include <string.h> //memset
namespace _tuz_private{

    const TUInt kNullCostValue=~(TUInt)0;
    #define _k_kBMatchStep   512  // skip by a big match

    static inline TInt _sstr_eqLen(const tuz_byte* ss_end,size_t maxSaveLength,
                            const  tuz_byte* matchedString,const tuz_byte* curString){
        TInt eqLen=0;
        while ((curString<ss_end)&&((*curString)==matchedString[eqLen])&&(eqLen<(TInt)maxSaveLength)){
            ++eqLen; ++curString;
        }
        return eqLen;
    }
    #define _MatchedAt(isSamePos,dict_pos){ \
        const size_t saveDictCost=coder.getSavedDictPosBit<isSamePos>(dict_pos,isHaveData);         \
        const size_t curSaveCost=cost[curi-1]+saveDictCost;             \
        for (TInt mLen=curMinLcp;mLen>=tuz_kMinDictMatchLen;--mLen){    \
            const size_t dictCost=curSaveCost+coder.getSavedDictLenBit<isSamePos>(mLen,dict_pos);   \
            const size_t ni=curi+mLen-1;    \
            const size_t cost_ni=cost[ni];  \
            if (dictCost<=cost_ni){         \
                cost[ni]=(TUInt)dictCost;   \
                matchLen[ni]=mLen;          \
                dictPos[ni]=(TPosInt)dict_pos;  \
            }else if (dictCost>cost_ni+4){  \
                break;                      \
            }   \
        }       \
        (*out_maxMatchLen)=((size_t)curMinLcp>(*out_maxMatchLen))?curMinLcp:(*out_maxMatchLen); \
        if (curMinLcp>=_k_kBMatchStep) return; }
    
    static inline size_t _lowBit64(uint64_t v){ //v!=0
    #if defined(__GNUC__) || defined(__clang__)
        return (size_t)__builtin_ctzll(v);
    #else
        size_t r=0;
        if ((v&0xFFFFFFFFu)==0) { v>>=32; r+=32; }
        if ((v&0xFFFF)==0) { v>>=16; r+=16; }
        if ((v&0xFF)==0) { v>>=8; r+=8; }
        while ((v&1)==0) { v>>=1; ++r; }
        return r;
    #endif
    }
    static inline size_t _highBit64(uint64_t v){ //v!=0
    #if defined(__GNUC__) || defined(__clang__)
        return (size_t)(63-__builtin_clzll(v));
    #else
        size_t r=0;
        if (v>>32) { v>>=32; r+=32; }
        if (v>>16) { v>>=16; r+=16; }
        if (v>>8) { v>>=8; r+=8; }
        while (v>>1) { v>>=1; ++r; }
        return r;
    #endif
    }
    static inline void _shl128(uint64_t* m,size_t n){
        if (n==0) return;
        if (n>=128){
            m[0]=0; m[1]=0;
        }else if (n>=64){
            m[1]=m[0]<<(n-64); m[0]=0;
        }else{
            m[1]=(m[1]<<n)|(m[0]>>(64-n)); m[0]<<=n;
        }
    }
    //equal length of a & b, from eqLen to maxEqLen
    static inline TInt _getEqLen(const tuz_byte* a,const tuz_byte* b,TInt eqLen,const TInt maxEqLen){
        while (eqLen+8<=maxEqLen){
            uint64_t va,vb;
            memcpy(&va,a+eqLen,8);
            memcpy(&vb,b+eqLen,8);
            if (va!=vb) break;
            eqLen+=8;
        }
        while ((eqLen<maxEqLen)&&(a[eqLen]==b[eqLen]))
            ++eqLen;
        return eqLen;
    }
    #define _setBit128(m,bit)   (m)[(bit)>>6]|=((uint64_t)1)<<((bit)&63)

    //a short pos (dict_pos<=kShortPos) with an equal run of curString & curString-(dict_pos+1)
    //  ending at eqEnd has equal length min(eqEnd-curString,maxSaveLength); short pos with the
    //  same eqEnd are kept in one group, and don't change until curString reaches eqEnd.
    //  isCapped: eqEnd==curString+maxSaveLength and the run may be longer, compared again next time.
    inline void TMatch::_alignByteMask(tuz_byte v,TInt curString){
        _shl128(shortByteMask[v],(size_t)(curString-shortByteMaskString[v]));
        shortByteMaskString[v]=curString;
    }
    void TMatch::_addShortGroup(TInt eqEnd,bool isCapped,size_t dict_pos){
        TShortGroup* groups=shortGroups.data();
        const size_t hi=(size_t)eqEnd&(kShortGroupHashSize-1);
        size_t i=shortGroupHash[hi];
        if ((i<shortGroupCount)&&(groups[i].eqEnd==eqEnd)&&(groups[i].isCapped==isCapped)){
            _setBit128(groups[i].mask,dict_pos);
            return;
        }
        for (i=0;i<shortGroupCount;++i){
            if ((groups[i].eqEnd==eqEnd)&&(groups[i].isCapped==isCapped)){
                _setBit128(groups[i].mask,dict_pos);
                shortGroupHash[hi]=(tuz_byte)i;
                return;
            }
        }
        shortGroupHash[hi]=(tuz_byte)shortGroupCount;
        TShortGroup& g=groups[shortGroupCount++];
        g.eqEnd=eqEnd;
        g.isCapped=isCapped;
        g.mask[0]=0; g.mask[1]=0;
        _setBit128(g.mask,dict_pos);
    }

    //out shortMatchList & shortMatchLen: dict_pos (from big to small) & its equal length to match,
    //  only the smallest dict_pos of a group; stop at a big match like _MatchedAt: it returns and
    //  the next position is not curString+1.
    size_t TMatch::_updateShortMatch(const TInt curString,const TInt kShortPos){
        const tuz_byte* src=sstring.src;
        const TInt srcSize=(TInt)sstring.size();
        const TInt maxSaveLength=(TInt)props.maxSaveLength;
        const size_t shortCount=(size_t)kShortPos+1;
        if (shortGroups.size()<shortCount){
            shortGroups.resize(shortCount);
            shortMatchList.resize(shortCount);
            shortMatchLen.resize(shortCount);
        }
        if (shortString+1!=curString){ //init
            shortGroupCount=0;
            memset(shortByteMask,0,sizeof(shortByteMask));
            memset(shortGroupHash,0,sizeof(shortGroupHash));
            for (size_t v=0;v<256;++v)
                shortByteMaskString[v]=curString;
            for (size_t dict_pos=0;dict_pos<shortCount;++dict_pos)
                _setBit128(shortByteMask[src[curString-1-(TInt)dict_pos]],dict_pos);
        }else{
            const tuz_byte v=src[curString-1];
            _alignByteMask(v,curString);
            shortByteMask[v][0]|=1;
        }
        shortString=curString;

        uint64_t active[2]={0,0};
        {//remove ended groups & compare capped group again
            TShortGroup* groups=shortGroups.data();
            size_t count=0;
            uint64_t capped[2]={0,0};
            TInt cappedEnd=0;
            for (size_t i=0;i<shortGroupCount;++i){
                const TShortGroup& g=groups[i];
                if (g.eqEnd<=curString) continue;
                if (g.isCapped){
                    capped[0]=g.mask[0]; capped[1]=g.mask[1];
                    cappedEnd=g.eqEnd;
                    continue;
                }
                active[0]|=g.mask[0]; active[1]|=g.mask[1];
                shortGroupHash[(size_t)g.eqEnd&(kShortGroupHashSize-1)]=(tuz_byte)count;
                groups[count++]=g;
            }
            shortGroupCount=count;
            for (size_t w=0;w<2;++w){
                while (capped[w]){
                    const size_t dict_pos=w*64+_lowBit64(capped[w]);
                    capped[w]&=capped[w]-1;
                    const bool isEq=(cappedEnd<srcSize)&&(src[cappedEnd]==src[cappedEnd-1-(TInt)dict_pos]);
                    const TInt eqEnd=cappedEnd+(isEq?1:0);
                    _addShortGroup(eqEnd,isEq&&(eqEnd-curString>=maxSaveLength),dict_pos);
                    _setBit128(active,dict_pos);
                }
            }
        }

        TInt bigDictPos=-1; //the biggest dict_pos with a big match
        TShortGroup* groups=shortGroups.data();
        for (size_t i=0;i<shortGroupCount;++i){
            const TShortGroup& g=groups[i];
            const TInt eqLen=(g.eqEnd-curString<maxSaveLength)?(g.eqEnd-curString):maxSaveLength;
            if (eqLen>=_k_kBMatchStep){
                const TInt hi=(TInt)(g.mask[1]?(64+_highBit64(g.mask[1])):_highBit64(g.mask[0]));
                bigDictPos=(hi>bigDictPos)?hi:bigDictPos;
            }
        }
        {//new equal runs
            const tuz_byte curByte=src[curString];
            _alignByteMask(curByte,curString);
            uint64_t fresh[2];
            fresh[0]=shortByteMask[curByte][0]&(~active[0]);
            fresh[1]=shortByteMask[curByte][1]&(~active[1]);
            if (shortCount<64){
                fresh[0]&=(((uint64_t)1)<<shortCount)-1;
                fresh[1]=0;
            }else{
                fresh[1]&=(((uint64_t)1)<<(shortCount-64))-1;
            }
            TInt maxEqLen=srcSize-curString;
            if (maxEqLen>maxSaveLength) maxEqLen=maxSaveLength;
            const tuz_byte* curStr=src+curString;
            for (size_t w=2;(w--)>0;){
                while (fresh[w]){
                    const size_t dict_pos=w*64+_highBit64(fresh[w]);
                    fresh[w]&=~(((uint64_t)1)<<(dict_pos&63));
                    if ((TInt)dict_pos<bigDictPos){
                        fresh[0]=0; fresh[1]=0;
                        break;
                    }
                    const tuz_byte* matchedStr=curStr-(dict_pos+1);
                    const TInt eqLen=_getEqLen(curStr,matchedStr,1,maxEqLen);
                    if (eqLen>=tuz_kMinDictMatchLen)
                        _addShortGroup(curString+eqLen,eqLen>=maxSaveLength,dict_pos);
                    if (eqLen>=_k_kBMatchStep){
                        bigDictPos=(TInt)dict_pos;
                        fresh[0]=0; fresh[1]=0;
                        break;
                    }
                }
            }
        }
        if (bigDictPos>=0)
            shortString=-1; //not all updated

        //out one dict_pos of each group, from big to small
        uint64_t outMask[2]={0,0};
        TLCPInt* outLens=shortOutLen;
        for (size_t i=0;i<shortGroupCount;++i){
            const TShortGroup& g=groups[i];
            const TInt eqLen=(g.eqEnd-curString<maxSaveLength)?(g.eqEnd-curString):maxSaveLength;
            if (eqLen<tuz_kMinDictMatchLen) continue;
            uint64_t m[2]={g.mask[0],g.mask[1]};
            if (bigDictPos>0){
                if (bigDictPos>=64){
                    m[0]=0;
                    m[1]&=~((((uint64_t)1)<<(bigDictPos-64))-1);
                }else{
                    m[0]&=~((((uint64_t)1)<<bigDictPos)-1);
                }
            }
            if ((m[0]|m[1])==0) continue;
            const size_t dict_pos=m[0]?_lowBit64(m[0]):(64+_lowBit64(m[1]));
            _setBit128(outMask,dict_pos);
            outLens[dict_pos]=(TLCPInt)eqLen;
        }
        TInt* matchList=shortMatchList.data();
        TLCPInt* matchLens=shortMatchLen.data();
        size_t matchCount=0;
        for (size_t w=2;(w--)>0;){
            while (outMask[w]){
                const size_t dict_pos=w*64+_highBit64(outMask[w]);
                outMask[w]&=~(((uint64_t)1)<<(dict_pos&63));
                matchList[matchCount]=(TInt)dict_pos;
                matchLens[matchCount]=outLens[dict_pos];
                ++matchCount;
            }
        }
        return matchCount;
    }

    void TMatch::_initMinDictLenBit(){
        minDictLenBit.resize(_k_kBMatchStep);
        for (size_t mLen=0;mLen<_k_kBMatchStep;++mLen){
            size_t bit=0;
            if (mLen>=tuz_kMinDictMatchLen){
                const size_t bitNear=coder.getSavedDictLenBit<false>(mLen,0);
                const size_t bitFar =coder.getSavedDictLenBit<false>(mLen,tuz_kBigPosForLen);
                bit=(bitNear<bitFar)?bitNear:bitFar;
            }
            minDictLenBit[mLen]=(TUInt)bit;
        }
    }

    void TMatch::_cost_match(const TInt curString,const size_t curi,
                             size_t* out_maxMatchLen,std::vector<TUInt>& cost){
        if (curString<=0) return;
        const size_t back_pos=dictPos[curi-1];
        const size_t isHaveData=((matchLen[curi-1]==0)&&(curi>1))?1:0;
        const TInt* SA=sstring.SA.data();
        const TInt it_cur=sstring.R[curString];

        if (isHaveData){ //same pos match
            TInt matchedString=curString-(TInt)(back_pos+1);
            TInt curMinLcp=_sstr_eqLen(sstring.src_end,props.maxSaveLength,
                                      sstring.src+matchedString,sstring.src+curString);
            if (curMinLcp>=tuz_kMinDictMatchLen)
                _MatchedAt(true,back_pos);
        }

        size_t maxDictPos=props.dictSize-1;
        if (maxDictPos>=(size_t)curString) maxDictPos=curString-1;
        #define _kShortPosForLen  (((1<<7)-1)-1)
        //#define _kShortPosForLen  (((1<<11)-1)-1) // better(0.2%) but slower(30%)
        #if (_kShortPosForLen>=((1<<7)-1))
        #   error short pos match need all short pos saved with the same bit count
        #endif
        const TInt kShortPos=(TInt)((maxDictPos>=_kShortPosForLen)?_kShortPosForLen:maxDictPos);
        {// short pos match
            const size_t matchCount=_updateShortMatch(curString,kShortPos);
            const TInt* matchList=shortMatchList.data();
            const TLCPInt* matchLens=shortMatchLen.data();
            if (matchCount>0){
                //all short pos have the same cost for the same mLen, so the one with the smallest dict_pos
                //  that reaches a mLen is saved; match from small dict_pos to big and stop at a mLen
                //  already done by a smaller dict_pos (below it, both make the same choices).
                if ((++lenStampCur)==0){
                    std::fill(lenStamp.begin(),lenStamp.end(),0);
                    lenStampCur=1;
                }
                const size_t curSaveCost=cost[curi-1]+coder.getSavedDictPosBit<false>(kShortPos,isHaveData);
                TInt maxEqLen=0;
                for (size_t i=matchCount;i-->0;){
                    const TInt dict_pos=matchList[i];
                    const TInt curMinLcp=matchLens[i];
                    maxEqLen=(curMinLcp>maxEqLen)?curMinLcp:maxEqLen;
                    for (TInt mLen=curMinLcp;mLen>=tuz_kMinDictMatchLen;--mLen){
                        if (lenStamp[mLen]==lenStampCur) break;
                        lenStamp[mLen]=lenStampCur;
                        const size_t dictCost=curSaveCost+coder.getSavedDictLenBit<false>(mLen,dict_pos);
                        const size_t ni=curi+mLen-1;
                        const size_t cost_ni=cost[ni];
                        if (dictCost<=cost_ni){
                            cost[ni]=(TUInt)dictCost;
                            matchLen[ni]=mLen;
                            dictPos[ni]=(TPosInt)dict_pos;
                        }else if (dictCost>cost_ni+4){
                            break;
                        }
                    }
                }
                (*out_maxMatchLen)=((size_t)maxEqLen>(*out_maxMatchLen))?maxEqLen:(*out_maxMatchLen);
                if (maxEqLen>=_k_kBMatchStep) return;
            }
            if (maxDictPos<=_kShortPosForLen) 
                return;
        }

        {//match by LCP
            TInt it_left=it_cur-1;
            const TInt it_left_end=-1;
            TInt it_right=it_cur+1;
            const TInt it_right_end=(TInt)sstring.size();
            const TLCPInt* LCP=sstring.LCP.data();

            //a match found by LCP has dict_pos>kShortPos, so its cost is at least
            //  minPosCost+minDictLenBit[mLen]; if that can't lower cost[curi+mLen-1] for
            //  every mLen<=curMinLcp, then this and all later (shorter) matches change nothing.
            const size_t minPosCost=cost[curi-1]+coder.getSavedDictPosBit<false>(kShortPos+1,isHaveData);
            TInt noGainLen=tuz_kMinDictMatchLen; //matches with len<noGainLen can't lower cost
            bool isFoundGainLen=false;

            TInt matchCountLimit=1024*2; //limit match time
            while (matchCountLimit>0){
                TInt curMinLcp=0;
                size_t dict_pos=-1;
                if (it_left>it_left_end){
                    curMinLcp=LCP[it_left];
                    dict_pos=(curString-SA[it_left])-1;
                }
                if (it_right<it_right_end){
                    TInt curLcp=LCP[it_right-1];
                    if (curLcp<curMinLcp){
                        --it_left;
                    }else{
                        size_t curPos=(curString-SA[it_right])-1;
                        ++it_right;
                        if (curLcp>curMinLcp){
                            curMinLcp=curLcp;
                            dict_pos=curPos;
                        }else{
                            dict_pos=(dict_pos<curPos)?dict_pos:curPos;
                            --it_left;
                        }
                    }
                }else{
                    --it_left;
                }
                if (curMinLcp<tuz_kMinDictMatchLen) 
                    return;
                if (curMinLcp<_k_kBMatchStep){ //skip search if no gain
                    if (curMinLcp<noGainLen)
                        return;
                    if (!isFoundGainLen){
                        for (;noGainLen<=curMinLcp;++noGainLen){
                            if (minPosCost+minDictLenBit[noGainLen]<=cost[curi+noGainLen-1]){
                                isFoundGainLen=true;
                                break;
                            }
                        }
                        if (!isFoundGainLen)
                            return;
                    }
                }

                while ((it_left>it_left_end)&&(LCP[it_left]>=curMinLcp)){
                    size_t curPos=(curString-SA[it_left])-1;
                    dict_pos=(dict_pos<curPos)?dict_pos:curPos;
                    if ((matchCountLimit--)<=0) break;
                    --it_left;
                }
                while ((it_right<it_right_end)&&(LCP[it_right-1]>=curMinLcp)){
                    size_t curPos=(curString-SA[it_right])-1;
                    dict_pos=(dict_pos<curPos)?dict_pos:curPos;
                    if ((matchCountLimit--)<=0) break;
                    ++it_right;
                }
                if ((kShortPos<dict_pos)&&(dict_pos<=maxDictPos))
                    _MatchedAt(false,dict_pos);
            }
        }
    }

void TMatch::_getCostByMatch(const tuz_byte* cur0,std::vector<TUInt>& cost){
    size_t costSize=cost.size();
    size_t unmatched_len=0;
    
    size_t i=1;
    while (i<costSize){
        if (matchLen[i-1]>0)
            unmatched_len=1;
        else
            ++unmatched_len;
        size_t curSaveDataCost=cost[i-unmatched_len]+coder.getSavedDataBit(unmatched_len);
        if (curSaveDataCost<cost[i]){
            cost[i]=(TUInt)curSaveDataCost;
            matchLen[i]=0;
            dictPos[i]=dictPos[i-unmatched_len];
        }

        const TInt curString=(TInt)(cur0+(i-1)-sstring.src);
        size_t maxMatchLen=tuz_kMinDictMatchLen;
        _cost_match(curString,i,&maxMatchLen,cost);
        
        i+=(maxMatchLen<_k_kBMatchStep)?1:maxMatchLen;
    }
}

void TMatch::_getCostByLiteralLen(std::vector<TUInt>& cost){
    //NOTE: current implementation does not search the optimal encoding path;
    // moreover, even if the optimal goal is to be achieved, it is recommended to handle it together in the _getCostByMatch().
    size_t costSize=cost.size();
    size_t unmatched_first=1;
    size_t unmatched_fill0_i=0;
    size_t best_win_cost=0;

    size_t i=unmatched_first;
    while (i<costSize){
        size_t mlen=matchLen[i];
        if (mlen==0){
            ++i;
            continue;
        }

        {//next match is same pos?
            const size_t curPos=dictPos[i];
            size_t nextPos=curPos-1; //not same
            size_t j=i+mlen;
            while (j<costSize){
                if (matchLen[j]>0){
                    nextPos=dictPos[j];
                    break;
                }
                ++j;
            }
            if (curPos==nextPos){
                i=j;
                continue; //next
            }

            i=i+mlen-1;
        }

        const size_t unmatched_len=i-unmatched_first+1;
        size_t curSaveDataCost=cost[unmatched_first-1]+coder.getSavedDataBit(unmatched_len);
        if (curSaveDataCost+best_win_cost<cost[i]){
            for (size_t j=unmatched_fill0_i+1;j<=i;++j)
                matchLen[j]=0;
            unmatched_fill0_i=i;
            best_win_cost=cost[i]-curSaveDataCost;
        }else if (curSaveDataCost+best_win_cost>=cost[i]+tuz_kMinLiteralLen){
            unmatched_first=i+1;
            unmatched_fill0_i=i;
            best_win_cost=0;
        }
        ++i;
    }
}

    static void _restoreMatch(size_t costSize,TPosInt* dictPos,TLCPInt* matchLen){
        size_t i_inc=costSize;
        while (i_inc>0) {
            size_t i=i_inc-1;
            TLCPInt mlen=matchLen[i];
            if (mlen>0){
                dictPos[i-mlen+1]=dictPos[i];
                matchLen[i-mlen+1]=mlen;
                i_inc-=mlen;
            }else{
                --i_inc;
            }
        }
    }
void TMatch::_initCost(std::vector<TUInt>& cost,size_t costSize){
    cost.clear();
    matchLen.clear();
    cost.resize(costSize,kNullCostValue);
    matchLen.resize(costSize,0);
    dictPos.clear();
    dictPos.resize(costSize,~(TPosInt)0);
    cost[0]=0;
    dictPos[0]=0;
    matchLen[0]=0;
}
void TMatch::_getCost(const tuz_byte* cur0){
    std::vector<TUInt> cost;
    const size_t costSize=(sstring.src_end-cur0)+1;

    _initCost(cost,costSize);
    _getCostByMatch(cur0,cost);
    sstring.clearMem();
    _restoreMatch(costSize,dictPos.data(),matchLen.data());
    
    if (props.isNeedLiteralLine)
        _getCostByLiteralLen(cost);
}

bool TMatch::match(const tuz_byte** out_matched,size_t* out_match_len,
                   const tuz_byte* cur){
    if (matchLen.empty())
        _getCost(cur);
    
    size_t costi=matchLen.size()-(sstring.src_end-cur);
    if (matchLen[costi]>0){
        *out_match_len=matchLen[costi];
        *out_matched=cur-dictPos[costi]-1;
        return true;
    }else{
        return false;
    }
}

}//namespace _tuz_private

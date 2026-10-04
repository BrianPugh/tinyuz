//fuzz_test.cpp
// for tinyuz: differential fuzz test of the compressor, see fuzz_test.sh
// usage: fuzz_test <seed> <count>
//   for each seed: make random data & props, compress, decompress & check,
//   and print one line with the props and a hash of the compressed code.
//   two builds of the compressor with the same output print the same lines.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include "decompress/tuz_dec.h"
#include "compress/tuz_enc.h"

static hpatch_uint64_t _rand_v;
static hpatch_uint64_t _rand(){ //xorshift64
    _rand_v^=_rand_v<<13; _rand_v^=_rand_v>>7; _rand_v^=_rand_v<<17;
    return _rand_v;
}
static size_t _randn(size_t n){ return n?(size_t)(_rand()%n):0; }

static size_t _randSize(){
    switch (_randn(10)){
        case 0: return _randn(4);
        case 1: case 2: case 3: return _randn(600);
        case 4: case 5: return _randn(8000);
        case 6: case 7: return _randn(80000);
        case 8: return 1024*256-64+_randn(128); //around kMinBestClipSize
        default: return 200000+_randn(500000); //some clips
    }
}

static void _randData(std::vector<tuz_byte>& data,size_t size){
    data.clear();
    const size_t alphabet=1+_randn(_randn(2)?4:256);
    while (data.size()<size){
        const size_t len=1+_randn(_randn(4)?64:3000);
        switch (_randn(8)){
            case 0: { //same byte run
                tuz_byte v=_randn(3)?0:(_randn(2)?0xFF:(tuz_byte)_rand());
                data.insert(data.end(),len,v);
            } break;
            case 1: { //random
                for (size_t i=0;i<len;++i) data.push_back((tuz_byte)_rand());
            } break;
            case 2: { //small alphabet
                for (size_t i=0;i<len;++i) data.push_back((tuz_byte)_randn(alphabet));
            } break;
            case 3: case 4: case 5: { //copy from a short, mid or far pos
                if (data.empty()) break;
                size_t maxPos=(_randn(3)==0)?128:(_randn(2)?4200:data.size());
                if (maxPos>data.size()) maxPos=data.size();
                const size_t pos=1+_randn(maxPos);
                for (size_t i=0;i<len;++i) data.push_back(data[data.size()-pos]);
            } break;
            case 6: { //copy with some bit flips, like a new firmware version
                if (data.empty()) break;
                const size_t pos=1+_randn(data.size());
                for (size_t i=0;i<len;++i){
                    tuz_byte v=data[data.size()-pos];
                    if (_randn(40)==0) v^=(tuz_byte)(1<<_randn(8));
                    data.push_back(v);
                }
            } break;
            default: { //periodic
                const size_t period=1+_randn(200);
                for (size_t i=0;i<len;++i)
                    data.push_back((data.size()>=period)?data[data.size()-period]:(tuz_byte)_rand());
            } break;
        }
    }
    data.resize(size);
}

static void _randProps(tuz_TCompressProps& props,size_t dataSize){
    switch (_randn(8)){
        case 0: props.dictSize=1+_randn(300); break;
        case 1: props.dictSize=126+_randn(5); break; //around the short pos
        case 2: case 3: props.dictSize=4096; break;
        case 4: props.dictSize=dataSize?(dataSize-1+_randn(3)):1; break;
        case 5: props.dictSize=((size_t)1)<<_randn(21); break;
        default: props.dictSize=1+_randn(1<<20); break;
    }
    if (props.dictSize<1) props.dictSize=1;
    switch (_randn(4)){
        case 0: props.maxSaveLength=tuz_kMinOfMaxSaveLength+_randn(3); break;
        case 1: props.maxSaveLength=tuz_kMaxOfMaxSaveLength; break;
        default: props.maxSaveLength=tuz_kMinOfMaxSaveLength
                    +_randn(tuz_kMaxOfMaxSaveLength-tuz_kMinOfMaxSaveLength+1); break;
    }
    props.isNeedLiteralLine=_randn(2)?true:false;
    props.threadNum=_randn(4)?1:(2+_randn(3));
}

int main(int argc,const char* argv[]){
    if (argc!=3){
        printf("usage: fuzz_test <seed> <count>\n");
        return 1;
    }
    const hpatch_uint64_t seed0=strtoull(argv[1],0,10);
    const long count=atol(argv[2]);
    long errorCount=0;
    std::vector<tuz_byte> data,code,undata;
    for (long k=0;k<count;++k){
        const hpatch_uint64_t seed=seed0+k;
        _rand_v=seed*0x9E3779B97F4A7C15ull+1; _rand(); _rand();
        const size_t dataSize=_randSize();
        _randData(data,dataSize);
        tuz_TCompressProps props;
        _randProps(props,dataSize);

        code.resize((size_t)tuz_maxCompressedSize(dataSize));
        hpatch_TStreamInput  in_data;
        hpatch_TStreamOutput out_code;
        mem_as_hStreamInput(&in_data,data.data(),data.data()+dataSize);
        mem_as_hStreamOutput(&out_code,code.data(),code.data()+code.size());
        const size_t codeSize=(size_t)tuz_compress(&out_code,&in_data,&props);

        undata.assign(dataSize+1,0);
        tuz_size_t undataSize=(tuz_size_t)dataSize;
        tuz_TResult ret=tuz_decompress_mem(code.data(),(tuz_size_t)codeSize,undata.data(),&undataSize);
        const bool isOk=((ret==tuz_OK)||(ret==tuz_STREAM_END))&&(undataSize==dataSize)
                        &&((dataSize==0)||(0==memcmp(undata.data(),data.data(),dataSize)));
        if (!isOk) ++errorCount;

        hpatch_uint64_t hash=1469598103934665603ull; //FNV-1a
        for (size_t i=0;i<codeSize;++i){ hash^=code[i]; hash*=1099511628211ull; }
        printf("%llu size=%zu dict=%zu maxLen=%zu literal=%d thread=%zu code=%zu hash=%016llx %s\n",
               (unsigned long long)seed,dataSize,(size_t)props.dictSize,(size_t)props.maxSaveLength,
               props.isNeedLiteralLine?1:0,(size_t)props.threadNum,codeSize,(unsigned long long)hash,
               isOk?"ok":"DECOMPRESS_ERROR");
        fflush(stdout);
    }
    return (errorCount==0)?0:1;
}

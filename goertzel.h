#ifndef GOERTZEL_H
#define GOERTZEL_H

#include "QtGlobal"
#include "cpx.h"

//Standard Reference Tones
struct DTMF {
    DTMF(quint16 h, quint16 l) {hi =h; lo=l;}
    quint16 hi;
    quint16 lo;
};
const DTMF DTMF_1(1209,697);
const DTMF DTMF_2(1336,697);
const DTMF DTMF_3(1477,697);
const DTMF DTMF_A(1633,697);
const DTMF DTMF_4(1209,770);
const DTMF DTMF_5(1336,770);
const DTMF DTMF_6(1477,770);
const DTMF DTMF_B(1633,770);
const DTMF DTMF_7(1209,852);
const DTMF DTMF_8(1336,852);
const DTMF DTMF_9(1477,852);
const DTMF DTMF_C(1633,852);
const DTMF DTMF_STAR(1209,941);
const DTMF DTMF_0(1336,941);
const DTMF DTMF_POUND(1477,941);
const DTMF DTMF_D(1633,941);

struct CTCSS {
    CTCSS(char *d, float f, float s) {
        strncpy(designation,d,2);
        freq = f;
        spacing = s;
    }
    char designation[2];
    float freq;
    float spacing;
};
const CTCSS CTCSS_1("XY",67.0,0);
const CTCSS CTCSS_2("XA",71.9,4.9);
const CTCSS CTCSS_3("WA",74.4,2.5);
const CTCSS CTCSS_4("??",0,0);//Not defined??
const CTCSS CTCSS_5("SP",79.7,2.7);
const CTCSS CTCSS_6("YZ",82.5,2.8);
const CTCSS CTCSS_7("YA",85.4,2.9);
const CTCSS CTCSS_8("YB",88.5,3.1);
const CTCSS CTCSS_9("ZZ",91.5,3.0);
const CTCSS CTCSS_10("ZA",94.8,3.3);
const CTCSS CTCSS_11("ZB",97.4,2.6);
const CTCSS CTCSS_12("1Z",100.0,2.6);
const CTCSS CTCSS_13("1A",103.5,3.5);
const CTCSS CTCSS_14("1B",107.2,3.7);
const CTCSS CTCSS_15("2Z",110.9,3.7);
const CTCSS CTCSS_16("2A",114.8,3.9);
const CTCSS CTCSS_17("2B",118.8,4.0);
const CTCSS CTCSS_18("3Z",123.0,4.2);
const CTCSS CTCSS_19("3A",127.3,4.3);
const CTCSS CTCSS_20("3B",131.8,4.5);
const CTCSS CTCSS_21("4Z",136.5,4.7);
const CTCSS CTCSS_22("4A",141.3,4.8);
const CTCSS CTCSS_23("4B",146.2,4.9);
const CTCSS CTCSS_24("5Z",151.4,5.2);
const CTCSS CTCSS_25("5A",156.7,5.3);
const CTCSS CTCSS_26("5B",162.2,5.5);
const CTCSS CTCSS_27("6Z",167.9,5.7);
const CTCSS CTCSS_28("6A",173.8,5.9);
const CTCSS CTCSS_29("6B",179.9,6.1);
const CTCSS CTCSS_30("7Z",186.2,6.3);
const CTCSS CTCSS_31("7A",192.8,6.6);
const CTCSS CTCSS_32("M1",203.5,10.7);

class Goertzel
{
public:
    Goertzel(int _n);
    //Sets internal values (coefficient) for freq
    void SetFreqHz(int freq, int sampleRate);
    void SetBinaryThreshold(float t);
    void FPNextSample(float input);
    void Q15NextSample (float input);

    int freqHz;
    int sampleRate;
    //output values
    CPX cpx;
    float power;
    float peakPower;
    bool binaryOutput;
    float binaryThreshold; //above is true, below or = is false

    //Filter coefficient, calculate with CalcCoefficient or table lookup
    float coeff;
    //#samples we need to process throught filter to get accurate result
    int blockSize;

private:
    //Internal values per EE Times article
    int k;
    float w;
    int binWidthHz;

    //These could be static in FPNextSample, but here for debugging and for use
    //if we try different algorithms
    //iteration counter
    int sampleCount;
    //Delay loop for filter
    float delay0;
    float delay1;
    float delay2;

};

#endif // GOERTZEL_H
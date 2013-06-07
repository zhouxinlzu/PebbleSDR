#ifndef AUDIOQT_H
#define AUDIOQT_H
//GPL license and attributions are in gpl.h and terms are included in this file by reference
#include "gpl.h"

#include "audio.h"
#include <QAudioOutput>
#include <QAudioFormat>

class AudioQT : public Audio
{
public:
	AudioQT(Receiver *r,int sr, int fpb, Settings *s);
	//Virtual functions
	//We may get input from some other source, if so inputSampleRate = 0
	int Start(int inputSampleRate, int outputSampleRate);
	int Stop();
	int Flush();
	int Pause();
	int Restart();
    void SendToOutput(CPX *, int outSamples);
	void ClearCounts();

	QStringList InputDeviceList();
	QStringList OutputDeviceList();

private:
	QAudioDeviceInfo qaDevice;
	QAudioOutput*    qaAudioOutput;
	//QIODevice*       qaOutput; // not owned
	QAudioFormat     qaFormat;
	QIODevice *dataSource;

    qint16 *outStreamBuffer;

	void DumpDeviceInfo(QAudioDeviceInfo device);


};

#endif // AUDIOQT_H

#pragma once
//GPL license and attributions are in gpl.h and terms are included in this file by reference
#include "gpl.h"
/*
Base class for SDR Receivers
*/
#include "global.h"

#include <QObject>
#include <QString>
#include <QThread>
#include <QSettings>
#include <QDialog>
#include "QSemaphore"

#include "devices/usbutil.h"
#include "audio.h"
#include "ui_iqbalanceoptions.h"
#include "ui_sdr.h"
#include "device_interfaces.h"


class Settings; //Can't include settings.h due to circular dependencies
class Receiver;
class SDRProducerThread;
class SDRConsumerThread;

class SDR:public QObject, public DeviceInterface
{
    Q_OBJECT
    //Let Qt meta-object know about our interface
    Q_INTERFACES(DeviceInterface)

	friend class SDRProducerThread; //Thread has access to private data
	friend class SDRConsumerThread;

public:

    SDR(Receiver *receiver, SDRDEVICE dev, Settings *_settings);
    virtual ~SDR(void);

    virtual QString GetPluginName() {return "Not Set";}
    virtual QString GetPluginDescription() {return "";}

    virtual bool Initialize(cbProcessIQData _callback) {return false;}

    virtual bool Connect(){return false;}
    virtual bool Disconnect(){return false;}
    virtual double SetFrequency(double fRequested,double fCurrent){return fCurrent;}
	//If SDR device is not using sound card, start/stop thread that returns data
	//Ignored unless overridden
    virtual void Start(){}
    virtual void Stop(){}
    virtual double GetStartupFrequency(){return 0;}
    virtual int GetStartupMode(){return 0;}
    virtual double GetHighLimit(){return 0;}
    virtual double GetLowLimit(){return 0;}
    virtual double GetGain(){return 1;}
    virtual QString GetDeviceName(){return "";}
	//Sample rate for some devices, like SDR-IQ, is dependent on bandwidth
	virtual int GetSampleRate();
    virtual int* GetSampleRates(int &len); //Returns array of allowable rates and length of array as ref

    //Display device option widget in settings dialog
    virtual void SetupOptionUi(QWidget *parent);
    //Called by settings to write device options to ini file
    virtual void WriteOptionUi() {}
    //Assume each device uses audio input.  Devices that don't should over-ride and return false to hide options
    virtual bool UsesAudioInput() {return true;}

    void StopProducerThread();
    void RunProducerThread();
    void StopConsumerThread();
    void RunConsumerThread();



	SDRDEVICE GetDevice();
	void SetDevice(SDRDEVICE m);

    //Proucer-Consumer buffer management
    void InitProducerConsumer(int _numDataBufs, int _producerBufferSize);

    bool IsFreeBufferAvailable();
    void AcquireFreeBuffer();
    void ReleaseFreeBuffer() {semNumFreeBuffers->release();}
    void IncrementProducerBuffer() {nextProducerDataBuf = (nextProducerDataBuf +1 ) % numDataBufs;}

    void AcquireFilledBuffer();
    void ReleaseFilledBuffer() {semNumFilledBuffers->release();}
    void IncrementConsumerBuffer() {nextConsumerDataBuf = (nextConsumerDataBuf +1 ) % numDataBufs;}

    void ShowSdrOptions(bool b);
    void InitSettings(QString fname);

protected:
    cbProcessIQData ProcessIQData;

    //Needed to determine when it's safe to fetch options for display
    bool connected;

    QDialog *sdrOptions;
    Ui::SdrOptions *sd;

    void ReadSettings();
    void WriteSettings();
    QSettings *qSettings;

    QStringList inputDevices;
    QStringList outputDevices;

	Audio *audioInput;
	Receiver *receiver;
	double startupFrequency; //0 means auto-set
	Settings *settings;
	bool isLibUsbLoaded;
	bool isFtdiLoaded;
    int sampleRates[10]; //Max 10 for testing

    //Producer/Consumer buffer management
    int numDataBufs; //Producer/Consumer buffers
    unsigned char **producerBuffer; //Array of buffers
    int producerBufferSize;
    int nextProducerDataBuf;
    int nextConsumerDataBuf;
    bool freeBufferOverflow;
    bool filledBufferOverflow;
    /*
      NumFreeBuffers starts at NUMDATABUFS and is decremented (acquire()) everytime the producer thread has new data.
      If it ever gets to zero, it will block forever and program will hang until consumer thread catches up.
      NumFreeBuffers is incremented (release()) in consumer thread when a buffer has been processed and can be reused.


    */
    QSemaphore *semNumFreeBuffers; //Init to NUMDATABUFS
    QSemaphore *semNumFilledBuffers;

	bool isThreadRunning;
	SDRProducerThread *producerThread;
	SDRConsumerThread *consumerThread;

private slots:
    void InputChanged(int i);
    void OutputChanged(int i);
    void StartupChanged(int i);
    void StartupFrequencyChanged();
    void SampleRateChanged(int i);
    void IQGainChanged(double i);
    void IQOrderChanged(int i);
    void BalancePhaseChanged(int v);
    void BalanceGainChanged(int v);
    void BalanceEnabledChanged(bool b);
    void BalanceReset();
    void ResetAllSettings(bool b);
    void CloseOptions(bool b);
    void TestBenchChanged(bool b);
};

//Generic thread that can be used in producer/consumer models for devices that don't use soundcard
class SDRProducerThread:public QThread
{
	Q_OBJECT
public:
    SDRProducerThread(DeviceInterface * s);
	void run();
	void stop();
	void setRefresh(int ms);

private:
    DeviceInterface *sdr;
	bool doRun;
	int msSleep;
};
class SDRConsumerThread:public QThread
{
	Q_OBJECT
public:
    SDRConsumerThread(DeviceInterface * s);
	void run();
	void stop();
	void setRefresh(int ms);

private:
    DeviceInterface *sdr;
	bool doRun;
	int msSleep;
};

//Replacement for windows Sleep() function
class Sleeper : public QThread
{
public:
    static void usleep(unsigned long usecs);
    static void msleep(unsigned long msecs);
    static void sleep(unsigned long secs);
};

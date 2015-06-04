#include "sdrplaydevice.h"

SDRPlayDevice::SDRPlayDevice():DeviceInterfaceBase()
{
	InitSettings("SDRPlay");
	optionUi = NULL;
	producerIBuf = producerQBuf = NULL;
	consumerBuffer = NULL;
	producerBuffer = NULL;
	samplesPerPacket = 0;

}

SDRPlayDevice::~SDRPlayDevice()
{
	if (producerIBuf != NULL)
		delete[] producerIBuf;
	if (producerQBuf != NULL)
		delete[] producerQBuf;
	if (consumerBuffer != NULL)
		delete consumerBuffer;
	if (producerBuffer != NULL)
		delete producerBuffer;
}

bool SDRPlayDevice::Initialize(cbProcessIQData _callback,
								  cbProcessBandscopeData _callbackBandscope,
								  cbProcessAudioData _callbackAudio, quint16 _framesPerBuffer)
{
	DeviceInterfaceBase::Initialize(_callback, _callbackBandscope, _callbackAudio, _framesPerBuffer);
	numProducerBuffers = 100;
#if 1
	//Remove if producer/consumer buffers are not used
	//This is set so we always get framesPerBuffer samples (factor in any necessary decimation)
	//ProducerConsumer allocates as array of bytes, so factor in size of sample data
	quint16 sampleDataSize = sizeof(CPX);
	readBufferSize = framesPerBuffer * sampleDataSize;

	producerIBuf = new short[framesPerBuffer * 2]; //2X what we need so we have overflow space
	producerQBuf = new short[framesPerBuffer * 2];
	producerIndex = 0;

	consumerBuffer = CPXBuf::malloc(framesPerBuffer);
	producerBuffer = CPXBuf::malloc(framesPerBuffer * 2);

	producerConsumer.Initialize(std::bind(&SDRPlayDevice::producerWorker, this, std::placeholders::_1),
		std::bind(&SDRPlayDevice::consumerWorker, this, std::placeholders::_1),numProducerBuffers, readBufferSize);
	//Must be called after Initialize
	producerConsumer.SetProducerInterval(sampleRate,readBufferSize);
	producerConsumer.SetConsumerInterval(sampleRate,readBufferSize);

#endif

	//Other constructor like init
	sampleRateMhz = sampleRate / 1000000.0; //Sample rate in Mhz, NOT Hz

	return true;
}

bool SDRPlayDevice::errorCheck(mir_sdr_ErrT err)
{
	switch (err) {
		case mir_sdr_Success:
			return true;
			break;
		case mir_sdr_Fail:
			return false;
			break;
		case mir_sdr_InvalidParam:
			qDebug()<<"SDRPLay InvalidParam error";
			break;
		case mir_sdr_OutOfRange:
			qDebug()<<"SDRPlay OutOfRange error";
			break;
		case mir_sdr_GainUpdateError:
			qDebug()<<"SDRPlay GainUpdate error";
			break;
		case mir_sdr_RfUpdateError:
			qDebug()<<"SDRPlay RfUpdate error";
			break;
		case mir_sdr_FsUpdateError:
			qDebug()<<"SDRPlay FsUpdate error";
			break;
		case mir_sdr_HwError:
			qDebug()<<"SDRPlay Hw error";
			break;
		case mir_sdr_AliasingError:
			qDebug()<<"SDRPlay Aliasing error";
			return true; //Ignore for now
			break;
		case mir_sdr_AlreadyInitialised:
			qDebug()<<"SDRPlay AlreadyInitialized error";
			break;
		case mir_sdr_NotInitialised:
			qDebug()<<"SDRPlay NotInitialized error";
			break;
		default:
			qDebug()<<"Unknown SDRPlay error";
			break;
	}
	return false;
}

bool SDRPlayDevice::Command(DeviceInterface::STANDARD_COMMANDS _cmd, QVariant _arg)
{
	switch (_cmd) {
		case CmdConnect:
			DeviceInterfaceBase::Connect();
			//Device specific code follows
			//Check version
			float ver;
			if (!errorCheck(mir_sdr_ApiVersion(&ver)))
				return false;
			qDebug()<<"SDRPLay Version: "<<ver;

			currentBand = band0; //Initial value to force band search on first frequency check

			//Will fail if SDRPlay is not connected
			if (!reinitMirics(deviceFrequency))
				return false;
			connected = true;
			running = false;
			return true;

		case CmdDisconnect:
			DeviceInterfaceBase::Disconnect();
			//Device specific code follows
			if (!errorCheck(mir_sdr_Uninit()))
				return false;
			connected = false;
			return true;

		case CmdStart:
			DeviceInterfaceBase::Start();
			//Device specific code follows
			running = true;
			producerConsumer.Start(true,true);

			return true;

		case CmdStop:
			DeviceInterfaceBase::Stop();
			//Device specific code follows
			running = false;
			producerConsumer.Stop();
			return true;

		case CmdReadSettings:
			DeviceInterfaceBase::ReadSettings();
			ReadSettings();
			return true;

		case CmdWriteSettings:
			DeviceInterfaceBase::WriteSettings();
			WriteSettings();
			return true;

		case CmdDisplayOptionUi:						//Arg is QWidget *parent
			//Use QVariant::fromValue() to pass, and value<type passed>() to get back
			this->SetupOptionUi(_arg.value<QWidget*>());
			return true;

		default:
			return false;
	}
}

QVariant SDRPlayDevice::Get(DeviceInterface::STANDARD_KEYS _key, quint16 _option)
{
	Q_UNUSED(_option);
	QStringList sl;

	switch (_key) {
		case PluginName:
			return "SDRPlay";
			break;
		case PluginDescription:
			return "SDRPlay (Mirics chips)";
			break;
		case DeviceName:
			return "SDRPlay";
		case DeviceType:
			return IQ_DEVICE;
		case DeviceSampleRates:
			//These correspond to SDRPlay IF Bandwidth options
			//sl<<"200000"<<"300000"<<"600000"<<"1536000"<<"5000000"<<"6000000"<<"7000000"<<"8000000";
			sl<<"2000000"<<"4000000"<<"6000000"<<"8000000";
			return sl;
			break;
		default:
			return DeviceInterfaceBase::Get(_key, _option);
	}
}

bool SDRPlayDevice::Set(DeviceInterface::STANDARD_KEYS _key, QVariant _value, quint16 _option)
{
	Q_UNUSED(_option);
	switch (_key) {
		case DeviceFrequency:
			return setFrequency(_value.toDouble());

		default:
			return DeviceInterfaceBase::Set(_key, _value, _option);
	}
}

void SDRPlayDevice::ReadSettings()
{
	//DeviceInterfaceBase takes care of reading and writing these values
	lowFrequency = 100000;
	highFrequency = 2000000000;
	deviceFrequency = lastFreq = 10000000;
	sampleRate = 2000000;

	DeviceInterfaceBase::ReadSettings();
	dcCorrectionMode = qSettings->value("dcCorrectionMode",0).toInt(); //0 = off
	totalGainReduction = qSettings->value("totalGainReduction",60).toInt(); //60db
	bandwidthKhz = (mir_sdr_Bw_MHzT) qSettings->value("bandwidthKhz",mir_sdr_BW_1_536).toInt();
	//bandwidth can not be > sampleRate
	if (bandwidthKhz > (sampleRate / 1000))
		bandwidthKhz = (mir_sdr_Bw_MHzT) (sampleRate / 1000);

	IFKhz = (mir_sdr_If_kHzT) qSettings->value("IFKhz",mir_sdr_IF_Zero).toInt();
}

void SDRPlayDevice::WriteSettings()
{
	DeviceInterfaceBase::WriteSettings();
	qSettings->setValue("dcCorrectionMode",dcCorrectionMode);
	qSettings->setValue("totalGainReduction",totalGainReduction);
	qSettings->setValue("bandwidthKhz",bandwidthKhz);
	qSettings->setValue("IFKhz",IFKhz);

}

void SDRPlayDevice::SetupOptionUi(QWidget *parent)
{
	//Arg is QWidget *parent
	if (optionUi != NULL)
		delete optionUi;

	//Change .h and this to correct class name for ui
	optionUi = new Ui::SDRPlayOptions();
	optionUi->setupUi(parent);
	parent->setVisible(true);

	//Set combo boxes
	optionUi->IFMode->addItem("Zero",mir_sdr_IF_Zero);
	optionUi->IFMode->addItem("450 Khz",mir_sdr_IF_0_450);
	optionUi->IFMode->addItem("1620 Khz",mir_sdr_IF_1_620);
	optionUi->IFMode->addItem("2048 Khz",mir_sdr_IF_2_048);

	optionUi->IFBw->addItem("0.200 Mhz",mir_sdr_BW_0_200);
	optionUi->IFBw->addItem("0.300 Mhz",mir_sdr_BW_0_300);
	optionUi->IFBw->addItem("0.600 Mhz",mir_sdr_BW_0_600);
	//Todo: Update whenever sample rate changes
	//Only allow BW selections that are <= sampleRate
	if (sampleRateMhz >= 1.536)
		optionUi->IFBw->addItem("1.536 Mhz",mir_sdr_BW_1_536);
	if (sampleRateMhz >= 5.000)
		optionUi->IFBw->addItem("5.000 Mhz",mir_sdr_BW_5_000);
	if (sampleRateMhz >= 6.000)
		optionUi->IFBw->addItem("6.000 Mhz",mir_sdr_BW_6_000);
	if (sampleRateMhz >= 7.000)
		optionUi->IFBw->addItem("7.000 Mhz",mir_sdr_BW_7_000);
	if (sampleRateMhz >= 8.000)
		optionUi->IFBw->addItem("8.000 Mhz",mir_sdr_BW_8_000);
	int cur = optionUi->IFBw->findData(bandwidthKhz);
	optionUi->IFBw->setCurrentIndex(cur);
	connect(optionUi->IFBw,SIGNAL(currentIndexChanged(int)),this,SLOT(IFBandwidthChanged(int)));

	optionUi->dcCorrection->addItem("Off", 0);
	optionUi->dcCorrection->addItem("One Shot", 4);
	optionUi->dcCorrection->addItem("Continuous", 5);
	cur = optionUi->dcCorrection->findData(dcCorrectionMode);
	optionUi->dcCorrection->setCurrentIndex(cur);
	connect(optionUi->dcCorrection,SIGNAL(currentIndexChanged(int)),this,SLOT(dcCorrectionChanged(int)));

	optionUi->totalGainReduction->setValue(totalGainReduction);
	connect(optionUi->totalGainReduction, SIGNAL(valueChanged(int)), this, SLOT(totalGainReductionChanged(int)));

}

void SDRPlayDevice::dcCorrectionChanged(int _item)
{
	int cur = _item;
	dcCorrectionMode = optionUi->dcCorrection->itemData(cur).toUInt();
	setDcMode(dcCorrectionMode, 1);
	WriteSettings();
}

void SDRPlayDevice::totalGainReductionChanged(int _value)
{
	totalGainReduction = _value;
	setGainReduction(totalGainReduction, 1, 0);
	WriteSettings();
}

void SDRPlayDevice::IFBandwidthChanged(int _item)
{
	int cur = _item;
	bandwidthKhz = (mir_sdr_Bw_MHzT)optionUi->IFBw->itemData(cur).toUInt();
	WriteSettings();
	reinitMirics(deviceFrequency);
}

//Initializes the mirics chips set
//Used initially and any time frequency changes to a new band
bool SDRPlayDevice::reinitMirics(double newFrequency)
{
	initInProgress.lock(); //Pause producer/consumer
	if (running) {
		//Unitinialize first
		if (!errorCheck(mir_sdr_Uninit())) {
			initInProgress.unlock();
			return false;
		}
	}
	if (!errorCheck(mir_sdr_Init(totalGainReduction, sampleRateMhz, newFrequency / 1000000.0, bandwidthKhz ,IFKhz , &samplesPerPacket))) {
		initInProgress.unlock();
		return false;
	}
	//Whenever we initialize, we also need to reset key values
	setDcMode(dcCorrectionMode, 1);

	initInProgress.unlock(); //re-start producer/consumer
	return true;
}

//gRdb = gain reduction in db
//abs = 0 Offset from current gr, abs = 1 Absolute
//syncUpdate = 0 Immedate, syncUpdate = 1 synchronous
bool SDRPlayDevice::setGainReduction(int gRdb, int abs, int syncUpdate)
{
	return (errorCheck(mir_sdr_SetGr(gRdb, abs, syncUpdate)));
}

bool SDRPlayDevice::setDcMode(int _dcCorrectionMode, int _speedUp)
{
	dcCorrectionMode = _dcCorrectionMode;
	if (errorCheck(mir_sdr_SetDcMode(dcCorrectionMode, _speedUp))) { //Speed up disabled (what is speed up?)
		return errorCheck(mir_sdr_SetDcTrackTime(63)); //Max Todo: review what this should be.  User adjustable?
	}
	return false;
}

bool SDRPlayDevice::setFrequency(double newFrequency)
{
	if (deviceFrequency == newFrequency || newFrequency == 0)
		return true;

	band newBand;
	quint16 setRFMode = 1; //0=apply freq as offset, 1=apply freq absolute
	quint16 syncUpdate = 0; //0=apply freq change immediately, 1=apply synchronously

#if 1
	//Bug?  With Mac API, I can't change freq within a band like 245-380 with absolute
	//If the new frequency is outside the current band, then we have to uninit and reinit in the new band
	if (newFrequency < currentBand.low || newFrequency > currentBand.high) {
		//Find new band
		if (newFrequency >= band1.low && newFrequency <= band1.high)
			newBand = band1;
		else if (newFrequency >= band2.low && newFrequency <= band2.high)
			newBand = band2;
		else if (newFrequency >= band3.low && newFrequency <= band3.high)
			newBand = band3;
		else if (newFrequency >= band4.low && newFrequency <= band4.high)
			newBand = band4;
		else if (newFrequency >= band5.low && newFrequency <= band5.high)
			newBand = band5;
		else if (newFrequency >= band6.low && newFrequency <= band6.high)
			newBand = band6;
		else {
			qDebug()<<"Frequency outside of bands";
			return false;
		}
		//Re-init with new band
		//Init takes freq in mhz
		if (!reinitMirics(newFrequency))
			return false;
		currentBand = newBand;

	} else {
		//SetRf takes freq in hz
		if (!errorCheck(mir_sdr_SetRf(newFrequency,setRFMode,syncUpdate))) {
			//Sometimes we get an error that previous update timed out, reset and try again
			mir_sdr_ResetUpdateFlags(false,true,false);
			if (!errorCheck(mir_sdr_SetRf(newFrequency,setRFMode,syncUpdate)))
				return false;
		}
	}
#else
	//Try offset logic from osmosdr
	//Higher freq = positive offset
	double delta = newFrequency - deviceFrequency;
	if (fabs(delta) < 10000.0) {
		if (!errorCheck(mir_sdr_SetRf(delta,0,syncUpdate)))
			return false;

	} else {
		if (!reinitMirics(newFrequency / 1000000.0))
			return false;
	}

#endif
	deviceFrequency = newFrequency;
	lastFreq = deviceFrequency;
	return true;

}


void SDRPlayDevice::producerWorker(cbProducerConsumerEvents _event)
{
	unsigned firstSampleNumber;
	//0 = no change, 1 = changed
	int gainReductionChanged;
	int rfFreqChanged;
	int sampleFreqChanged;

	CPX *producerFreeBufPtr; //Treat as array of CPX

#if 0
	static short maxSample = 0;
	static short minSample = 0;
#endif

	static quint32 lastSampleNumber = 0;
	switch (_event) {
		case cbProducerConsumerEvents::Start:
			break;

		case cbProducerConsumerEvents::Run:
			if (!connected || !running)
				return;

			//Returns one packet (samplesPerPacket) of data int I and Q buffers
			//We want to read enough data to fill producerIbuf and producerQbuf with framesPerBuffer
			while(running) {
				//If init is in progress (locked), wait for it to complete
				initInProgress.lock();

				//Read all the I's into 1st part of buffer, and Q's into 2nd part
				if (!errorCheck(mir_sdr_ReadPacket(producerIBuf, producerQBuf, &firstSampleNumber, &gainReductionChanged,
					&rfFreqChanged, &sampleFreqChanged))) {
					initInProgress.unlock();
					return; //Handle error
				}
				initInProgress.unlock();

				if (lastSampleNumber != 0 && firstSampleNumber > lastSampleNumber &&
						firstSampleNumber != lastSampleNumber + samplesPerPacket) {
					qDebug()<<"Lost samples "<< lastSampleNumber<<" "<<firstSampleNumber<<" "<<samplesPerPacket;
				}
				lastSampleNumber = firstSampleNumber;
#if 0
				if (rfFreqChanged) {
					//If center freq changed since last packet, throw this one away and get next one
					//Should make frequency changes instant, regardless of packet size
					continue;
				}
#endif
				//Save in producerBuffer (sized to handle overflow
				//Make sure samplesPerPacket is initialized before producer starts
				for (int i=0; i<samplesPerPacket; i++) {
					producerBuffer[producerIndex].re = producerIBuf[i];
					producerBuffer[producerIndex].im = producerQBuf[i];
					producerIndex++;

					if (producerIndex >= framesPerBuffer) {
						if ((producerFreeBufPtr = (CPX *)producerConsumer.AcquireFreeBuffer()) == NULL) {
							qDebug()<<"No free buffers available.  producerIndex = "<<producerIndex <<
									  "samplesPerPacket = "<<samplesPerPacket;
							producerIndex = 0;
							return;
						}

						for (int j=0; j<framesPerBuffer; j++) {
							producerFreeBufPtr[j] = producerBuffer[j];

	#if 0
							//For testing device sample format
							//maxSample = 32764
							//minSample = -32768
							if (producerIBuf[j] > maxSample) {
								maxSample = producerIBuf[j];
								qDebug()<<"New Max sample "<<maxSample;
							}
							if (producerQBuf[j] > maxSample) {
								maxSample = producerQBuf[j];
								qDebug()<<"New Max sample "<<maxSample;
							}
							if (producerIBuf[j] < minSample) {
								minSample = producerIBuf[j];
								qDebug()<<"New Min sample "<<minSample;
							}
							if (producerQBuf[j] < minSample) {
								minSample = producerQBuf[j];
								qDebug()<<"New Min sample "<<minSample;
							}
	#endif

						}
						//Process any remaining samples in packet if any
						producerIndex = 0;
						//Continue with i from outer for(int i=0;;)
						for (++i; i< samplesPerPacket; i++) {
							producerBuffer[producerIndex].re = producerIBuf[i];
							producerBuffer[producerIndex].im = producerQBuf[i];
							producerIndex++;
						}

						producerConsumer.ReleaseFilledBuffer();
						//qDebug()<<"Released filled buffer";
						return;
					}
				} //for(;i<samplesPerPacket;)
			} //while(running)
			return;

			break;
		case cbProducerConsumerEvents::Stop:
			break;
	}
}

void SDRPlayDevice::consumerWorker(cbProducerConsumerEvents _event)
{
	CPX *consumerFilledBufferPtr;
	//NOTE: API doc says data is returned in 16bit integer shorts
	//Actual min/max samples in producerWorker confirms this, we get min of -32556 and max of 32764
	//There is some confusion in other open source code which normalizes using 12bit min/max or 1/4096
	//double normalizeIQ = 1.0 / 4096.0;
	double normalizeIQ = 1.0 / 32767.0;

	switch (_event) {
		case cbProducerConsumerEvents::Start:
			break;
		case cbProducerConsumerEvents::Run:
			if (!connected || !running)
				return;

			//We always want to consume everything we have, producer will eventually block if we're not consuming fast enough
			while (running && producerConsumer.GetNumFilledBufs() > 0) {
				//Wait for data to be available from producer
				if ((consumerFilledBufferPtr = (CPX *)producerConsumer.AcquireFilledBuffer()) == NULL) {
					//qDebug()<<"No filled buffer available";
					return;
				}

				//Process data in filled buffer and convert to Pebble format in consumerBuffer
				//Filled buffers always have framesPerBuffer I/Q samples
				for (int i=0; i<framesPerBuffer; i++) {
					//Fill consumerBuffer with normalized -1 to +1 data
					//SDRPlay swaps I/Q from norm, reverse here
					consumerBuffer[i].re = consumerFilledBufferPtr[i].im * normalizeIQ;
					consumerBuffer[i].im = consumerFilledBufferPtr[i].re * normalizeIQ;
				}

				//perform.StartPerformance("ProcessIQ");
				ProcessIQData(consumerBuffer,framesPerBuffer);
				//perform.StopPerformance(1000);
				//We don't release a free buffer until ProcessIQData returns because that would also allow inBuffer to be reused
				producerConsumer.ReleaseFreeBuffer();

			}
			break;
		case cbProducerConsumerEvents::Stop:
			break;
	}
}
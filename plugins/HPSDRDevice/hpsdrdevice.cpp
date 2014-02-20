#include "hpsdrdevice.h"
#include <QMessageBox>

#if 0
References
Metis: http://www.w7ay.net/site/Software/Metis%20Framework/index.html

#endif

HPSDRDevice::HPSDRDevice():DeviceInterfaceBase()
{
	InitSettings("HPSDR-USB");

	ozyFX2FW.clear();
	ozyFW=0;
	mercuryFW=0;
	penelopeFW=0;

	ReadSettings();

	optionUi = NULL;
	inputBuffer = NULL;
	outputBuffer = NULL;
	producerFreeBufPtr = NULL;

}

HPSDRDevice::~HPSDRDevice()
{
	WriteSettings();

	if (usbUtil.IsUSBLoaded()) {
		usbUtil.ReleaseInterface(0);
		usbUtil.CloseDevice();
	}
}

bool HPSDRDevice::Initialize(cbProcessIQData _callback, quint16 _framesPerBuffer)
{
	ProcessIQData = _callback;
	framesPerBuffer = _framesPerBuffer;

	//Frames are 512 bytes, defined by HPSDR protocol
	//Each frame has 3 sync and 5 Command and control bytes, leaving 504 for data
	//Each sample is 3 bytes(24 bits) x 2 for I and Q plus 2 bytes for Mic-Line
	//Experiment with different values here
	//Lets start with makeing sure every fetch gets at least 2048 samples
	inputBufferSize = OZY_FRAME_SIZE * qRound(framesPerBuffer / 63.0 * 0.5);

	if (outputBuffer != NULL)
		delete[] outputBuffer;
	outputBuffer = new unsigned char[OZY_FRAME_SIZE]; //Max #bytes we can send to Ozy

	if (inputBuffer != NULL)
		delete[] inputBuffer;
	inputBuffer = new unsigned char[inputBufferSize];

	connected = false;
	numProducerBuffers = 50;
	readBufferSize = framesPerBuffer * sizeof(CPX);
	producerConsumer.Initialize(std::bind(&HPSDRDevice::producerWorker, this, std::placeholders::_1),
		std::bind(&HPSDRDevice::consumerWorker, this, std::placeholders::_1),numProducerBuffers, readBufferSize);
	//Must be called after Initialize
	//Sample rate and size must be in consistent units - bytes
	producerConsumer.SetPollingInterval(sampleRate*sizeof(CPX),inputBufferSize);

	return true;
}

bool HPSDRDevice::Connect()
{
	//Was in constructor for internal implemenation, but should be in Initialize or connect
	//Note: This uses libusb 0.1 not 1.0
	// See http://www.libusb.org/ and http://libusb.sourceforge.net/doc/ for details of API
	// Look for libusb0.dll, libusb0.sys, lib0_x86.dll, libusb0_x64.dll, libusb0_x64.sys if you have problems
	//Todo: Is there a crash bug if no libusb drivers are found on system?  Lib should handle and return error code

	//Set up libusb
	if (!usbUtil.IsUSBLoaded()) {
		//Explicit load.  DLL may not exist on users system, in which case we can only suppoprt non-USB devices like SoftRock Lite
		if (!usbUtil.LoadUsb()) {
			QMessageBox::information(NULL,"Pebble","libusb0 could not be loaded.");
			return false;
		} else {
			usbUtil.InitUSB();
		}

	}

	if (!Open())
		return false;
	connected = true;

#if 0
	//Testing access to device descriptor strings
	unsigned char sn[256];
	libusb_device_descriptor desc;
	libusb_get_device_descriptor(dev,&desc);
	if (desc.iSerialNumber) {
		//int ret = usb_get_string_simple(hDev, desc.iSerialNumber, sn, sizeof(sn));
		int ret = libusb_get_string_descriptor(hDev,desc.iSerialNumber,0,sn,sizeof(sn));
		if (ret > 0) {
			qDebug()<<"Serial number :"<<sn;
		} else {
			qDebug()<<"No Serial number";
		}
	}
#endif

	//This setting allows the user to load and manage the firmware directly if Pebble has a problem for some reason
	//If no init flag, just send Ozy config
	//If no firmware is loaded this will return error
	if (sNoInit) {
		return SendConfig();
	}


	//Check firmware version to see if already loaded
	ozyFX2FW = GetFirmwareInfo();
	if (ozyFX2FW!=NULL) {
		qDebug()<<"Firmware String: "<<ozyFX2FW;
		if (!SendConfig()) {
			return false;
		}
	} else {
		qDebug()<<"Firmware String not found, initializing";
#if(0)
		QMessageBox loadMsg;
		loadMsg.setIcon(QMessageBox::Information);
		loadMsg.setWindowTitle("Pebble");
		loadMsg.setText("Loading Firmware and FPGA, please wait");
		//QTimer::oneShot(2000, &loading, SLOT(hide()));  //Use this to auto hide after N ms

		//Bug: Nothing shows in box.  exec() works but waits for user to click something
		loadMsg.show();
#endif
		//Firmware and FPGA code are lost on when Ozy loses power
		//We reload on every connect to make sure
		if (!ResetCPU(true))
			return false;
		if (!LoadFirmware(sFW))
			return false;
		if (!ResetCPU(false))
			return false;
		if (!Close())
			return false;

		//Give Ozy a chance to process firmware
		QThread::sleep(5);

		if (!Open())
			return false;

		if(!SetLED(1,true))
			return false;
		if(!LoadFpga(sFpga))
			return false;
		if(!SetLED(1,false))
			return false;
		//loadMsg.hide();

		if (!Close())
			return false;

		if (!Open())
			return false;

		//Ozy needs a few Command and Control frames to select the desired clock sources before data can be received
		//So send C&C a few times
		for (int i=0; i<6; i++) {
			if (!SendConfig()) {
				return false;
			}
		}

		ozyFX2FW = GetFirmwareInfo();
		if (ozyFX2FW!=NULL) {
			qDebug()<<"New Firmware FX2FW version :"<<ozyFX2FW;
		} else {
			qDebug()<<"Firmware upload failed";
			return false;
		}

	}

	//        1E 00 - Reset chip
	//        12 01 - set digital interface active
	//        08 15 - D/A on, mic input, mic 20dB boost
	//        08 14 - ditto but no mic boost
	//        0C 00 - All chip power on
	//        0E 02 - Slave, 16 bit, I2S
	//        10 00 - 48k, Normal mode
	//        0A 00 - turn D/A mute off

	//char foo[] = {0x1e,0x00};
	//int count = WriteI2C(0x1a,foo,2);

	return true;
}

bool HPSDRDevice::Disconnect()
{
	connected = false;
	return Close();
}

void HPSDRDevice::Start()
{
	sampleCount = 0;
	producerConsumer.Start(true,true);
}

void HPSDRDevice::Stop()
{
	producerConsumer.Stop();
}

//Review and rename common in device base
void HPSDRDevice::ReadSettings()
{
	//Set defaults before we call base ReadSettings
	startupFrequency = 10000000;
	lowFrequency = 150000;
	highFrequency = 33000000;
	startupDemodMode = dmAM;
	iqGain = 1.0;

	DeviceInterfaceBase::ReadSettings();
	//Determine sSpeed from sampleRate
	switch (sampleRate) {
	case 48000:
		sSpeed = C1_SPEED_48KHZ;
		break;
	case 96000:
		sSpeed = C1_SPEED_96KHZ;
		break;
	case 192000:
		sSpeed = C1_SPEED_192KHZ;
		break;
	case 384000:
		sSpeed = C1_SPEED_384KHZ;
		break;
	}


	sGainPreampOn = qSettings->value("GainPreampOn",3.0).toDouble();
	sGainPreampOff = qSettings->value("GainPreampOff",20.0).toDouble();
	sPID = qSettings->value("PID",0x0007).toInt();
	sVID = qSettings->value("VID",0xfffe).toInt();
	//NOTE: I had problems with various combinations of hex and rbf files
	//The most current ones I found were the 'PennyMerge' version which can be found here
	//svn://64.245.179.219/svn/repos_sdr_windows/PowerSDR/branches/kd5tfd/PennyMerge
	sFpga = qSettings->value("FPGA","ozy_janus.rbf").toString();
	sFW = qSettings->value("FW","ozyfw-sdr1k.hex").toString();

	//SDR-WIDGET
	//Skips firmware check and upload
	sNoInit = qSettings->value("NoInit",false).toBool();

	//Harware options
	//sSpeed = qSettings->value("Speed",C1_SPEED_48KHZ).toInt();
	sPTT = qSettings->value("PTT",C0_MOX_INACTIVE).toInt();
	s10Mhz = qSettings->value("10Mhz",C1_10MHZ_MERCURY).toInt();
	s122Mhz = qSettings->value("122Mhz",C1_122MHZ_MERCURY).toInt();
	sConfig = qSettings->value("AtlasConfig",C1_CONFIG_MERCURY).toInt();
	sMic = qSettings->value("MicSource",C1_MIC_PENELOPE).toInt();
	sMode = qSettings->value("Mode",C2_MODE_OTHERS).toInt();
	sPreamp = qSettings->value("LT2208Preamp",C3_LT2208_PREAMP_ON).toInt();
	sDither = qSettings->value("LT2208Dither",C3_LT2208_DITHER_OFF).toInt();
	sRandom = qSettings->value("LT2208Random",C3_LT2208_RANDOM_OFF).toInt();
}

void HPSDRDevice::WriteSettings()
{
	DeviceInterfaceBase::WriteSettings();
	//Device specific settings follow
	qSettings->setValue("GainPreampOn",sGainPreampOn);
	qSettings->setValue("GainPreampOff",sGainPreampOff);
	qSettings->setValue("PID",sPID);
	qSettings->setValue("VID",sVID);
	qSettings->setValue("FPGA",sFpga);
	qSettings->setValue("FW",sFW);
	qSettings->setValue("NoInit",sNoInit);
	qSettings->setValue("PTT",sPTT);
	qSettings->setValue("10Mhz",s10Mhz);
	qSettings->setValue("122Mhz",s122Mhz);
	qSettings->setValue("AtlasConfig",sConfig);
	qSettings->setValue("MicSource",sMic);
	qSettings->setValue("Mode",sMode);
	qSettings->setValue("LT2208Preamp",sPreamp);
	qSettings->setValue("LT2208Dither",sDither);
	qSettings->setValue("LT2208Random",sRandom);

	qSettings->sync();
}

QVariant HPSDRDevice::Get(DeviceInterface::STANDARD_KEYS _key, quint16 _option)
{
	Q_UNUSED(_option);

	switch (_key) {
		case PluginName:
			return "HPSDR-USB";
			break;
		case PluginDescription:
			return "HPSDR Devices";
			break;
		case DeviceName:
			return "HPSDR-USB";
		case DeviceType:
			return INTERNAL_IQ;
		case DeviceSampleRates:
			return QStringList()<<"48000"<<"96000"<<"192000"<<"384000";
		case CustomKey1:
			if (sPreamp == C3_LT2208_PREAMP_ON)
				return sGainPreampOn;
			else
				return sGainPreampOff;

		default:
			return DeviceInterfaceBase::Get(_key, _option);
	}
}

bool HPSDRDevice::Set(DeviceInterface::STANDARD_KEYS _key, QVariant _value, quint16 _option)
{
	Q_UNUSED(_option);

	switch (_key) {
		case DeviceFrequency: {
			char cmd[5];
			qint32 freq = _value.toDouble();
			cmd[0] = C0_FREQUENCY; //Bits set receiver# (if > 1) are we setting for
			cmd[1] = freq >> 24;
			cmd[2] = freq >> 16;
			cmd[3] = freq >> 8;
			cmd[4] = freq;

			if (!WriteOutputBuffer(cmd)) {
				qDebug()<<"HPSDR failed to update frequency";
				return deviceFrequency;
			}
			deviceFrequency = freq;
			lastFreq = freq;
			return freq;
		}
		default:
			return DeviceInterfaceBase::Set(_key, _value, _option);
	}
}

void HPSDRDevice::producerWorker(cbProducerConsumerEvents _event)
{
	Q_UNUSED(_event);
	//We're using blocking I/O, so BulkTransfer will wait until we get size
	//So fetch 1 frame at a time
	int result;
	int actual;
	result = usbUtil.BulkTransfer(IN_ENDPOINT6,inputBuffer,inputBufferSize,&actual,OZY_TIMEOUT);
	if (result == 0) {
		int remainingBytes = actual;

#if 0
	result = usb_bulk_read(hDev, IN_ENDPOINT6, inputBuffer, sizeof(inputBuffer), OZY_TIMEOUT);
	if (result > 0) {
		int remainingBytes = result;
#endif
		int len;
		int bufPtr = 0;
		while (remainingBytes > 0){
			len = (remainingBytes < OZY_FRAME_SIZE) ? remainingBytes : OZY_FRAME_SIZE;
			ProcessInputFrame(&inputBuffer[bufPtr],len);
			bufPtr += len;
			remainingBytes -= len;
		}

	} else {
		qDebug()<<"Error or timeout in usb_bulk_read";
	}

}
//Processes each 512 byte frame from inputBuffer
//len is usually BUFSIZE
bool HPSDRDevice::ProcessInputFrame(unsigned char *buf, int len)
{
	if(len < OZY_FRAME_SIZE)
		qDebug()<<"Partial frame received";

	int b=0; //Buffer index
	if (buf[b++]==OZY_SYNC && buf[b++]==OZY_SYNC && buf[b++]==OZY_SYNC){
		char ctrlBuf[5];
		ctrlBuf[0] = buf[b++];
		ctrlBuf[1] = buf[b++];
		ctrlBuf[2] = buf[b++];
		ctrlBuf[3] = buf[b++];
		ctrlBuf[4] = buf[b++];
		//Extract info from control bytes,  bit 3 has type
		switch ((ctrlBuf[0]>>3) & 0x1f) {
		case 0:
			//Status information
			//int lt2208ADCOverflow = ctrlBuf[0]&0x01;
			//int HermesIO1 = ctrlBuf[0]&0x02;
			//int HermesIO2 = ctrlBuf[0]&0x04;
			//int HermesIO3 = ctrlBuf[0]&0x08;
			//Firmware versions
			mercuryFW = ctrlBuf[2];
			penelopeFW = ctrlBuf[3];
			ozyFW = ctrlBuf[4];
			break;
		case 1:
			//Forward Power
			break;
		case 2:
			//Reverse Power
			break;
		case 3:
			//AIN4 & AIN6
			break;
		}
		//Extract I/Q and add to buffer
		CPX cpx;
		double mic;
		while (b < len) {
			if (sampleCount == 0) {
				//Get a new buffer
				producerFreeBufPtr = (CPX*)producerConsumer.AcquireFreeBuffer();
				if (producerFreeBufPtr == NULL)
					return false;
			}

			//24bit samples
			//NOTE: The (signed char) and (unsigned char) casts are critical in order for the bit shift to work correctly
			//Otherwise the MSB won't have the sign set correctly
			cpx.re = (signed char)buf[b++]<<16;
			cpx.re += (unsigned char)buf[b++]<<8;
			cpx.re += (unsigned char)buf[b++];
			cpx.re /= 8388607.0; //Convert to +/- float from 24bit int

			cpx.im = (signed char)buf[b++]<<16;
			cpx.im += (unsigned char)buf[b++]<<8;
			cpx.im += (unsigned char)buf[b++];
			cpx.im /= 8388607.0; //Convert to +/- float

			//Skip mic-line sample for now
			mic = (signed char)buf[b++]<<8;
			mic += (unsigned char)buf[b++];
			mic /= 32767.0;

			//Add to dataBuf and see if we have enough samples
			producerFreeBufPtr[sampleCount++] = cpx;
			if (sampleCount == framesPerBuffer) {
				sampleCount = 0;
				producerFreeBufPtr = NULL;
				producerConsumer.ReleaseFilledBuffer();
			}
		}
		return true;
	}
	qDebug()<<"Invalid sync in data frame";
	return false; //Invalid sync
}

void HPSDRDevice::consumerWorker(cbProducerConsumerEvents _event)
{
	Q_UNUSED(_event);
	//Wait for data from the producer thread
	unsigned char *dataBuf = producerConsumer.AcquireFilledBuffer();

	//Got data, process
	ProcessIQData((CPX *)dataBuf,2048); //!!Check size, CPX samples?

	producerConsumer.ReleaseFreeBuffer();
}

//Gets called several times from connect
bool HPSDRDevice::Open()
{
	//usbUtil.ListDevices();
	//Keep dev for later use, it has all the descriptor information
	if (!usbUtil.FindAndOpenDevice(sPID,sVID,0))
		return false;
	// Default is config #0, Select config #1 for SoftRock
	// Can't claim interface if config != 1
	if (!usbUtil.SetConfiguration(1))
		return false;

	// Claim interface #0.
	if (!usbUtil.Claim_interface(0))
		return false;
	//Some comments say that altinterface needs to be set to use usb_bulk_read, doesn't appear to be the case
	//int result = usb_set_altinterface(hDev,0);
	//if (result < 0)
	//	return false;

	return true;
}

bool HPSDRDevice::Close()
{
	int result;
	//Attempting to release interface caused error next time we open().  Why?
	//result = usb_release_interface(hDev,0);
	result = usbUtil.CloseDevice();
	return true;
}


//Configuration bytes are always sent as a group, ie you can't change just one
//This is the only place we do this to avoid inconsistencies
bool HPSDRDevice::SendConfig()
{
	char cmd[5];
	cmd[0] = sPTT;
	cmd[1] = sSpeed | s10Mhz | s122Mhz | sConfig | sMic;
	cmd[2] = sMode;
	cmd[3] = sPreamp | sDither | sRandom;
	//If C4_DUPLEX_ON, then tuning doesn't work
	cmd[4] = C4_DUPLEX_OFF;

	memset(outputBuffer,0x00,OZY_FRAME_SIZE);
	return WriteOutputBuffer(cmd);
}

void HPSDRDevice::preampChanged(bool b)
{
	//Weird gcc bug, can't use static consts in this construct
	//sPreamp = b ? C3_LT2208_PREAMP_ON : C3_LT2208_PREAMP_OFF;
	if(b)
		sPreamp = C3_LT2208_PREAMP_ON;
	else
		sPreamp = C3_LT2208_PREAMP_OFF;

	//Save to hpsdr.ini
	WriteSettings();

	//Change immediately
	SendConfig();
}
void HPSDRDevice::ditherChanged(bool b)
{
	if(b)
		sDither = C3_LT2208_DITHER_ON;
	else
		sDither = C3_LT2208_DITHER_OFF;

	//Save to hpsdr.ini
	WriteSettings();

	//Change immediately
	SendConfig();
}

void HPSDRDevice::randomChanged(bool b)
{
	if(b)
		sRandom = C3_LT2208_RANDOM_ON;
	else
		sRandom = C3_LT2208_RANDOM_OFF;

	//Save to hpsdr.ini
	WriteSettings();

	//Change immediately
	SendConfig();
}

void HPSDRDevice::optionsAccepted()
{
	//Save settings are are not immediate mode

	//Save to hpsdr.ini
	WriteSettings();

}

void HPSDRDevice::SetupOptionUi(QWidget *parent)
{
	if (optionUi != NULL)
		delete optionUi;
	optionUi = new Ui::HPSDROptions();
	optionUi->setupUi(parent);
	parent->setVisible(true);

#if 0
	QFont smFont = settings->smFont;
	QFont medFont = settings->medFont;
	QFont lgFont = settings->lgFont;

	optionUi->enableDither->setFont(medFont);
	optionUi->enablePreamp->setFont(medFont);
	optionUi->enableRandom->setFont(medFont);
	optionUi->hasExcalibur->setFont(medFont);
	optionUi->hasJanus->setFont(medFont);
	optionUi->hasMercury->setFont(medFont);
	optionUi->hasMetis->setFont(medFont);
	optionUi->hasOzy->setFont(medFont);
	optionUi->hasPenelope->setFont(medFont);
	optionUi->mercuryLabel->setFont(medFont);
	optionUi->ozyFX2Label->setFont(medFont);
	optionUi->ozyLabel->setFont(medFont);
	optionUi->penelopeLabel->setFont(medFont);
#endif

	connect(optionUi->enablePreamp,SIGNAL(clicked(bool)),this,SLOT(preampChanged(bool)));
	connect(optionUi->enableDither,SIGNAL(clicked(bool)),this,SLOT(ditherChanged(bool)));
	connect(optionUi->enableRandom,SIGNAL(clicked(bool)),this,SLOT(randomChanged(bool)));

	if (sPreamp == C3_LT2208_PREAMP_ON)
		optionUi->enablePreamp->setChecked(true);
	if (sDither == C3_LT2208_DITHER_ON)
		optionUi->enableDither->setChecked(true);
	if (sRandom == C3_LT2208_RANDOM_ON)
		optionUi->enableRandom->setChecked(true);

	//hardwired config for now, just ozy and mercury
	optionUi->hasOzy->setChecked(true);
	optionUi->hasMetis->setEnabled(false);
	optionUi->hasMercury->setChecked(true);
	optionUi->hasMercury->setEnabled(false);
	optionUi->hasPenelope->setEnabled(false);
	optionUi->hasJanus->setEnabled(false);
	optionUi->hasExcalibur->setEnabled(false);

	if (!ozyFX2FW.isEmpty())
		optionUi->ozyFX2Label->setText("Ozy FX2 (.hex): " + ozyFX2FW);
	else
		optionUi->ozyFX2Label->setText("Ozy FX2 (.hex): No Firmware");

	if (ozyFW)
		optionUi->ozyLabel->setText("Ozy FPGA (.rbf): " + QString::number(ozyFW));
	else
		optionUi->ozyLabel->setText("Ozy FPGA (.rbf): No Firmware");

	if (mercuryFW)
		optionUi->mercuryLabel->setText("Mercury FPGA (.rbf): " + QString::number(mercuryFW));
	else
		optionUi->mercuryLabel->setText("Mercury FPGA (.rbf): No Firmware");

	if (penelopeFW)
		optionUi->penelopeLabel->setText("Penelope FPGA (.rbf): " + QString::number(penelopeFW));
	else
		optionUi->penelopeLabel->setText("Penelope FPGA (.rbf): No Firmware");

}

bool HPSDRDevice::WriteOutputBuffer(char * commandBuf)
{
	if (!connected)
		return false;

	outputBuffer[0] = OZY_SYNC;
	outputBuffer[1] = OZY_SYNC;
	outputBuffer[2] = OZY_SYNC;

	//Just handle commands for now
	outputBuffer[3] = commandBuf[0];
	outputBuffer[4] = commandBuf[1];
	outputBuffer[5] = commandBuf[2];
	outputBuffer[6] = commandBuf[3];
	outputBuffer[7] = commandBuf[4];

	int result;
	int actual;
	result = usbUtil.BulkTransfer(OUT_ENDPOINT2,outputBuffer,OZY_FRAME_SIZE,&actual,OZY_TIMEOUT);
	if (result < 0 || actual != OZY_FRAME_SIZE) {
		qDebug()<<"Error writing output buffer";
		return false;
	}
#if 0
	result = usb_bulk_write(hDev, OUT_ENDPOINT2, outputBuffer, BUFSIZE, OZY_TIMEOUT);
	if (result != BUFSIZE) {
		qDebug()<<"Error writing output buffer";
		return false;
	}
#endif
	return true;
}

int HPSDRDevice::WriteRam(int fx2StartAddr, unsigned char *buf, int count)
{
	int bytesWritten = 0;
	int bytesRemaining = count;
	while (bytesWritten < count) {
		bytesRemaining = count - bytesWritten;
		//Chunk into blocks of MAX_PACKET_SIZE
		int numBytes = bytesRemaining > MAX_PACKET_SIZE ? MAX_PACKET_SIZE : bytesRemaining;
		int result = usbUtil.ControlMsg(VENDOR_REQ_TYPE_OUT,OZY_WRITE_RAM,
			fx2StartAddr+bytesWritten, 0, buf+bytesWritten, numBytes, OZY_TIMEOUT);
		//int result = usb_control_msg(hDev, VENDOR_REQ_TYPE_OUT, OZY_WRITE_RAM,
		//	fx2StartAddr+bytesWritten, 0, buf+bytesWritten, numBytes, OZY_TIMEOUT);
		if (result > 0)
			bytesWritten += result;
		else
			return bytesWritten; //Timeout or error, return what was written
	}
	return bytesWritten;
}

//Turns reset mode on/off
bool HPSDRDevice::ResetCPU(bool reset)
{
	unsigned char writeBuf;
	writeBuf = reset ? 1:0;
	int bytesWritten = WriteRam(0xe600,&writeBuf,1);
	return bytesWritten == 1 ? true:false;
}

bool HPSDRDevice::LoadFpga(QString filename)
{
	qDebug()<<"Loading FPGA";
	QString path = QCoreApplication::applicationDirPath();
#ifdef Q_OS_MAC
		//Pebble.app/contents/macos = 25
		path.chop(25);
#endif

	QFile rbfFile(path + "/PebbleData/" + filename);
	//QT assumes binary unless we add QIODevice::Text
	if (!rbfFile.open(QIODevice::ReadOnly)) {
		qDebug()<<"FPGA file not found";
		return false;
	}
	//Tell Ozy loading FPGA (no data sent)
	int result;
	result = usbUtil.ControlMsg(VENDOR_REQ_TYPE_OUT, OZY_LOAD_FPGA, 0, FL_BEGIN, NULL, 0, OZY_TIMEOUT);
	//result = usb_control_msg(hDev, VENDOR_REQ_TYPE_OUT, OZY_LOAD_FPGA, 0, FL_BEGIN, NULL, 0, OZY_TIMEOUT);
	if (result < 0) {
		rbfFile.close();
		return false;
	}

	//Read file and send to Ozy 64bytes at a time
	int bytesRead = 0;
	unsigned char buf[MAX_PACKET_SIZE];
	while((bytesRead = rbfFile.read((char*)buf,sizeof(buf))) > 0) {

		result = usbUtil.ControlMsg(VENDOR_REQ_TYPE_OUT, OZY_LOAD_FPGA, 0, FL_XFER,  buf, bytesRead, OZY_TIMEOUT);
		//result = usb_control_msg(hDev, VENDOR_REQ_TYPE_OUT, OZY_LOAD_FPGA, 0, FL_XFER, buf, bytesRead, OZY_TIMEOUT);
		if (result < 0) {
			rbfFile.close();
			return false;
		}
	}
	rbfFile.close();
	//Tell Ozy done with FPGA load
	result = usbUtil.ControlMsg(VENDOR_REQ_TYPE_OUT, OZY_LOAD_FPGA, 0, FL_END, NULL, 0, OZY_TIMEOUT);
	//result = usb_control_msg(hDev, VENDOR_REQ_TYPE_OUT, OZY_LOAD_FPGA, 0, FL_END, NULL, 0, OZY_TIMEOUT);
	if (result < 0)
		return false;
	qDebug()<<"FPGA upload complete";
	return true;
}

//NOTE: Similar functions are available in QString and QByteArray
int HPSDRDevice::HexByteToUInt(char c)
{
	c = tolower(c);
	if ( c >= '0' && c <= '9' ) {
		return c - '0';
	}
	else if ( c >= 'a' && c <= 'f' ) {
		return 10 + (c - 'a');
	}
	return -1; //Not hex
}

int HPSDRDevice::HexBytesToUInt(char * buf, int len)
{
	unsigned result = 0;
	int value;
	for ( int i = 0; i < len; i++ ) {
		value = HexByteToUInt(buf[i]);
		if (value < 0)
			return -1;
		//Bytes are in LSB->MSB order?
		result *= 16;
		result += value;
	}
	return result;

}

bool HPSDRDevice::LoadFirmware(QString filename)
{
	qDebug()<<"Loading firmware";

	QString path = QCoreApplication::applicationDirPath();
#ifdef Q_OS_MAC
		//Pebble.app/contents/macos = 25
		path.chop(25);
#endif

	QFile rbfFile(path + "/PebbleData/" + filename);
	if (!rbfFile.open(QIODevice::ReadOnly))
		return false;
	//Read file and send to Ozy 64bytes at a time
	int bytesRead = 0;
	char buf[1030];
	unsigned char writeBuf[256];
	int numLine = 0;
	int dataTuples = 0;
	int address = 0;
	int type = 0;
	int checksum = 0;
	int value = 0;
	int result = 0;
	int index = 0;
	//Read one line at a time
	while((bytesRead = rbfFile.readLine(buf,sizeof(buf))) > 0) {
		numLine++;
		if (buf[0] != ':') {
			qDebug() << "Bad hex record in " << filename << "line " << numLine;
			rbfFile.close();
			return false;
		}
		//Example
		//: 09 0080 00 02 00 6B 00 02 00 6B 00 02 9B
		//  L  ADR  TY DATA..................DATA CS
		//0:0	= :
		//1:2	= L: # data tuples in hex (09 or 18 bytes)
		//3:6	= ADR: address (0080)
		//7:8	= TY: type (00 = record)
		//9:..	= data tuples, 2 bytes each
		//EOL	= CS: checksum (02)

		dataTuples = HexBytesToUInt(&buf[1],2);
		address = HexBytesToUInt(&buf[3],4);
		type = HexBytesToUInt(&buf[7],2);
		if (dataTuples <0 || address < 0 || type < 0){
			qDebug() << "Invalid length, address or type in record";
			rbfFile.close();
			return false;
		}
		switch (type){
		case 0:
			checksum = (unsigned char)(dataTuples + (address & 0xff) + ((address >> 8) + type));

			index = 9;
			for (int i=0; i< dataTuples; i++) {
				value = HexBytesToUInt(&buf[index],2);
				if (value < 0) {
					qDebug()<<"Invalid hex in line "<<numLine;
					rbfFile.close();
					return false;
				}
				writeBuf[i] = value;
				checksum += value;
				index += 2;
			}
			if (((checksum + HexBytesToUInt(&buf[index],2)) & 0xff) != 0) {
				qDebug()<<"Invalid Checksum in line "<<numLine;
				return false;
			}
			//qDebug()<<"Address "<<address;
			result = WriteRam(address, writeBuf, dataTuples);

			if (result < 0) {
				qDebug()<<"Error in WriteRam() uploading firmware";
				rbfFile.close();
				return false;
			}
			break;
		case 1:
			//EOF, Complete
			break;
		default:
			qDebug()<<"Invalid record type";
			return false;
		}
	}
	rbfFile.close();
	qDebug()<<"Firmware update complete";
	return true;
}



QString HPSDRDevice::GetFirmwareInfo()
{
	int index = 0;
	unsigned char bytes[9]; //Version string is 8 char + null
	int size = 8;
	int result = usbUtil.ControlMsg(VENDOR_REQ_TYPE_IN, OZY_SDR1K_CTL, SDR1K_CTL_READ_VERSION,index, bytes, size, OZY_TIMEOUT);
	//int result = usb_control_msg(hDev, VENDOR_REQ_TYPE_IN, OZY_SDR1K_CTL, SDR1K_CTL_READ_VERSION, index, bytes, size, OZY_TIMEOUT);
	if (result > 0) {
		bytes[result] = 0x00; //Null terminate string
		return (char*)bytes;
	} else {
		return NULL;
	}
}
int HPSDRDevice::WriteI2C(int i2c_addr, unsigned char byte[], int length)
{
	if (length < 1 || length > MAX_PACKET_SIZE) {
		return 0;
	} else {
	int result = usbUtil.ControlMsg(
	   VENDOR_REQ_TYPE_OUT,
	   OZY_I2C_WRITE,
	   i2c_addr,
	   0,
	   byte,
	   length,
	   OZY_TIMEOUT
	   );
	if (result < 0)
		return 0;
	return result;
	}
}

//Ozy LEDs: #1 used to indicate FPGA upload in process
bool HPSDRDevice::SetLED(int which, bool on)
{
	int val;
	val = on ? 1:0;
	//usb_control_msg(hDev,reqType, req, value, data, index, length, timeout);
	//int result = usb_control_msg(hDev, VENDOR_REQ_TYPE_OUT, OZY_SET_LED,val, which, NULL, 0, OZY_TIMEOUT);
	int result = usbUtil.ControlMsg(VENDOR_REQ_TYPE_OUT, OZY_SET_LED, val, which,  NULL, 0, OZY_TIMEOUT);
	return result<0 ? false:true;
}
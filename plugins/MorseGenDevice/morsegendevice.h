#ifndef MORSEGENDEVICE_H
#define MORSEGENDEVICE_H

//GPL license and attributions are in gpl.h and terms are included in this file by reference
#include "gpl.h"
#include <QObject>
#include "deviceinterfacebase.h"
#include "ui_morsegendevice.h"
#include "morsegen.h"

class MorseGenDevice : public QObject, public DeviceInterfaceBase
{
	Q_OBJECT

	//Exports, FILE is optional
	//IID must be same that caller is looking for, defined in interfaces file
	Q_PLUGIN_METADATA(IID DeviceInterface_iid)
	//Let Qt meta-object know about our interface
	Q_INTERFACES(DeviceInterface)

public:
	MorseGenDevice();
	~MorseGenDevice();

	//Required
	bool initialize(CB_ProcessIQData _callback,
					CB_ProcessBandscopeData _callbackBandscope,
					CB_ProcessAudioData _callbackAudio,
					quint16 _framesPerBuffer);
	bool command(StandardCommands _cmd, QVariant _arg);
	QVariant get(StandardKeys _key, QVariant _option = 0);
	bool set(StandardKeys _key, QVariant _value, QVariant _option = 0);

private slots:
	void loadPresetClicked(bool clicked);
	void savePresetClicked(bool clicked);
	void updateAllFields();
	void updateGen1Fields();
	void updateGen2Fields();
	void updateGen3Fields();
	void updateGen4Fields();
	void updateGen5Fields();
	void updateNoiseFields();
	void updatePresetName();

	void gen1BrowseClicked();
	void gen2BrowseClicked();
	void gen3BrowseClicked();
	void gen4BrowseClicked();
	void gen5BrowseClicked();
private:
	enum SampleTextChoices {
		ST_SAMPLE1, //Sample1 text from ini file
		ST_SAMPLE2, //Sample2 text from ini file
		ST_SAMPLE3, //Sample3 text from ini file
		ST_FILE, //Sample4 text from ini file
		ST_WORDS,	//Random words from table
		ST_ABBREV,	//Random morse abbeviations
		ST_TABLE,	//All characters in morse table
		ST_END};
	void readSettings();
	void writeSettings();
	void producerWorker(cbProducerConsumerEvents _event);
	void consumerWorker(cbProducerConsumerEvents _event);
	void setupOptionUi(QWidget *parent);

	//Work buffer for consumer to convert device format data to CPX Pebble format data
	CPX *m_consumerBuffer;
	quint16 m_producerIndex;
	CPX *m_producerFreeBufPtr; //Treat as array of CPX
	CPX *m_consumerFilledBufPtr; //Treat as array of CPX

	Ui::MorseGenOptions *m_optionUi;

	QElapsedTimer m_elapsedTimer;
	qint64 m_nsPerBuffer; //How fast do we have to output a buffer of data to match recorded sample rate

	static const quint32 c_dbFadeRange = 20; //For random fade generation

	CPX *m_outBuf;

	CPX *m_outBuf1;
	CPX *m_outBuf2;
	CPX *m_outBuf3;
	CPX *m_outBuf4;
	CPX *m_outBuf5;

	static const quint32 c_numGenerators = 5;
	MorseGen *m_morseGen1;
	MorseGen *m_morseGen2;
	MorseGen *m_morseGen3;
	MorseGen *m_morseGen4;
	MorseGen *m_morseGen5;

	QString m_sampleText[ST_END];
	CPX nextNoiseSample(double _dbGain);
	qint32 m_dbNoiseAmp;
	double m_noiseAmp;

	void generate(CPX *out);

	//Do not change order without update default initializers below
	struct GenSettings {
		//Don't change order without changing initializers below
		bool enabled;
		quint32 text;
		QString fileName;
		double freq;
		double amp;
		quint32 wpm;
		quint32 rise;
		bool fade;
		bool fist;
	};

	GenSettings m_gs1;
	GenSettings m_gs2;
	GenSettings m_gs3;
	GenSettings m_gs4;
	GenSettings m_gs5;
	GenSettings m_gs1Default = {true, 0, "", 1000, -40, 10, 5, false, false};
	GenSettings m_gs2Default = {true, 1, "", 2000, -40, 20, 5, false, false};
	GenSettings m_gs3Default = {true, 2, "", 3000, -40, 30, 5, false, false};
	GenSettings m_gs4Default = {true, 3, "", 4000, -40, 40, 5, false, false};
	GenSettings m_gs5Default = {true, 4, "", 5000, -40, 50, 5, false, false};

	static const quint32 c_numPresets = 5;
	QString m_preset1Name;
	GenSettings m_preset1[c_numGenerators];
	QString m_preset2Name;
	GenSettings m_preset2[c_numGenerators];
	QString m_preset3Name;
	GenSettings m_preset3[c_numGenerators];
	QString m_preset4Name;
	GenSettings m_preset4[c_numGenerators];
	QString m_preset5Name;
	GenSettings m_preset5[c_numGenerators];

	void updateGenerators();

	QMutex m_mutex; //Locks generator changes when producer thead is calling generate()
	void setGen1Ui(GenSettings gs);
	void setGen2Ui(GenSettings gs);
	void setGen3Ui(GenSettings gs);
	void setGen4Ui(GenSettings gs);
	void setGen5Ui(GenSettings gs);

	void initSourceBox(QComboBox *box);
	void initWpmBox(QComboBox *box);
	void initDbBox(QComboBox *box);

	void readGenSettings(quint32 genNum, GenSettings *gs);
	void writeGenSettings(quint32 genNum, GenSettings *gs);

	QString m_morseFileName;
	QString getSampleText(GenSettings gs);
};
#endif // MORSEGENDEVICE_H

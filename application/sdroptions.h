#ifndef SDROPTIONS_H
#define SDROPTIONS_H
#include <QDialog>
#include "device_interfaces.h"
#include "ui_sdr.h"
#include "audio.h"

class SdrOptions:public QObject
{
	Q_OBJECT
public:
	SdrOptions();
	~SdrOptions();
	void ShowSdrOptions(DeviceInterface *_di, bool b);

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

private:
	DeviceInterface *di;

	QDialog *sdrOptionsDialog;
	Ui::SdrOptions *sd;


	QStringList inputDevices;
	QStringList outputDevices;

};

#endif // SDROPTIONS_H
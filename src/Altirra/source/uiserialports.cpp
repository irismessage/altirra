#include "stdafx.h"
#include "Dialog.h"
#include "resource.h"
#include "simulator.h"
#include "rs232.h"

extern ATSimulator g_sim;

class ATUISerialPortsDialog : public VDDialogFrameW32 {
public:
	ATUISerialPortsDialog();

protected:
	void OnDataExchange(bool write);
	bool OnCommand(uint32 id, uint32 extcode);
	void UpdateEnables();

	ATRS232Config	mConfig;
	bool mbEnabled;
};

ATUISerialPortsDialog::ATUISerialPortsDialog()
	: VDDialogFrameW32(IDD_SERIAL_PORTS)
{
}

void ATUISerialPortsDialog::OnDataExchange(bool write) {
	if (write) {
		mConfig.mbTelnetEmulation = IsButtonChecked(IDC_TELNET);

		bool enabled = IsButtonChecked(IDC_ENABLE);
		g_sim.SetRS232Enabled(enabled);

		if (enabled)
			g_sim.GetRS232()->SetConfig(mConfig);
	} else {
		bool enabled = g_sim.IsRS232Enabled();

		CheckButton(IDC_ENABLE, enabled);

		mbEnabled = enabled;

		if (enabled)
			g_sim.GetRS232()->GetConfig(mConfig);

		CheckButton(IDC_TELNET, mConfig.mbTelnetEmulation);

		UpdateEnables();
	}
}

bool ATUISerialPortsDialog::OnCommand(uint32 id, uint32 extcode) {
	if (id == IDC_ENABLE) {
		bool enabled = IsButtonChecked(IDC_ENABLE);

		if (mbEnabled != enabled) {
			mbEnabled = enabled;

			UpdateEnables();
		}
	}

	return false;
}

void ATUISerialPortsDialog::UpdateEnables() {
	bool enabled = IsButtonChecked(IDC_ENABLE);

	EnableControl(IDC_TELNET, enabled);
}

void ATUIShowSerialPortsDialog(VDGUIHandle h) {
	ATUISerialPortsDialog dlg;

	dlg.ShowDialog(h);
}

#include "stdafx.h"
#include "uifilebrowser.h"

ATUIFileBrowser::ATUIFileBrowser() {
}

ATUIFileBrowser::~ATUIFileBrowser() {
}

void ATUIFileBrowser::OnCreate() {
	mpListView = new ATUIListView;
	AddChild(mpListView);
	mpListView->SetDockMode(kATUIDockMode_Fill);

	mpBottomContainer = new ATUIContainer;
	AddChild(mpBottomContainer);
	mpBottomContainer->SetArea(vdrect32(0, 0, 0, 26));
	mpBottomContainer->SetDockMode(kATUIDockMode_Bottom);

	mpTextEdit = new ATUITextEdit;
	mpBottomContainer->AddChild(mpTextEdit);
	mpTextEdit->SetDockMode(kATUIDockMode_Fill);
	mpTextEdit->SetText(L"c:\\foo\\bar");

	mpButtonOK = new ATUIButton;
	mpBottomContainer->AddChild(mpButtonOK);
	mpButtonOK->SetArea(vdrect32(0, 0, 75, 20));
	mpButtonOK->SetText(L"OK");
	mpButtonOK->SetDockMode(kATUIDockMode_Right);

	mpButtonCancel = new ATUIButton;
	mpBottomContainer->AddChild(mpButtonCancel);
	mpButtonCancel->SetArea(vdrect32(0, 0, 75, 20));
	mpButtonCancel->SetText(L"Cancel");
	mpButtonCancel->SetDockMode(kATUIDockMode_Right);

	OnSize();
}

void ATUIFileBrowser::OnDestroy() {
	mpListView.clear();
	mpTextEdit.clear();
	mpBottomContainer.clear();
	mpButtonOK.clear();
	mpButtonCancel.clear();

	RemoveAllChildren();
}

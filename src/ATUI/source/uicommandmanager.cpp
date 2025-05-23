//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2012 Avery Lee
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.
//
//	This program is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU General Public License for more details.
//
//	You should have received a copy of the GNU General Public License
//	along with this program; if not, write to the Free Software
//	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#include <stdafx.h>
#include <vd2/system/error.h>
#include <vd2/system/hash.h>
#include <vd2/Dita/accel.h>
#include <at/atui/uicommandmanager.h>

////////////////////////////////////////////////////////////////////////////////

bool ATUICommandContext::GetArg(size_t index, VDStringW& s) const {
	s.clear();

	if (index >= mInArgs.size())
		return false;

	s = mInArgs[index];
	return true;
}

void ATUICommandContext::SetArg(size_t index, const wchar_t *s) {
	if (mbRecordArgs) {
		if (mOutArgs.size() <= index)
			mOutArgs.resize(index + 1);

		mOutArgs[index] = s;
	}
}

void ATUICommandContext::MarkCompleted(bool succeeded) {
	if (!mbCommandCompleted) {
		mbCommandCompleted = true;
		mbCommandCancelled = !succeeded;

		if (mpParent)
			mpParent->NotifyCommandExecuted(*this);
	}
}

////////////////////////////////////////////////////////////////////////////////

ATUICommandManager::ATUICommandManager() {
	memset(mpHashTable, 0, sizeof mpHashTable);
}

ATUICommandManager::~ATUICommandManager() {
}

void ATUICommandManager::RegisterCommand(const ATUICommand *cmd) {
	const uint32 hash = VDHashString32(cmd->mpName);
	const uint32 htidx = hash % kHashTableSize;

	Node *node = mAllocator.Allocate<Node>();
	node->mpNext = mpHashTable[htidx];
	node->mHash = hash;
	node->mpCmd = cmd;

	mpHashTable[htidx] = node;
}

void ATUICommandManager::RegisterCommands(const ATUICommand *cmd, size_t n) {
	while(n--)
		RegisterCommand(cmd++);
}

const ATUICommand *ATUICommandManager::GetCommand(const char *str) const {
	const uint32 hash = VDHashString32(str);
	const uint32 htidx = hash % kHashTableSize;

	for(const Node *node = mpHashTable[htidx]; node; node = node->mpNext) {
		const ATUICommand *cmd = node->mpCmd;

		if (!strcmp(cmd->mpName, str)) {
			return cmd;
		}
	}

	return NULL;
}

bool ATUICommandManager::ExecuteCommand(const char *str, const ATUICommandOptions& ctx) {
	const ATUICommand *cmd = GetCommand(str);

	if (!cmd)
		return false;

	return ExecuteCommand(*cmd, ctx);
}

bool ATUICommandManager::ExecuteCommand(const ATUICommand& cmd, const ATUICommandOptions& ctx) {
	if (cmd.mpTestFn && !cmd.mpTestFn())
		return false;

	if (mpCommandContext && mpCommandContext->GetRefCount() > 1) {
		if (!mpCommandContext->mbCommandCompleted) {
			mpCommandContext->mbCommandCompleted = true;
			mpCommandContext->mbCommandCancelled = true;
		}

		mpCommandContext->mpParent = nullptr;
		mpCommandContext = nullptr;
	}

	if (!mpCommandContext) {
		mpCommandContext = new ATUICommandContext;
		mpCommandContext->mpParent = this;
	}

	mpCommandContext->mbCommandCancelled = false;
	mpCommandContext->mbCommandCompleted = false;
	mpCommandContext->mpCommand = &cmd;
	mpCommandContext->mbRecordArgs = !mOnCommandExecuted.IsEmpty();
	mpCommandContext->mOutArgs.clear();

	static_cast<ATUICommandOptions&>(*mpCommandContext) = ctx;

	cmd.mpExecuteFn(*mpCommandContext);

	if (mpCommandContext->GetRefCount() == 1)
		mpCommandContext->MarkCompleted(true);

	return true;
}

bool ATUICommandManager::ExecuteCommandNT(const ATUICommand& cmd, const ATUICommandOptions& ctx) noexcept {
	try {
		ExecuteCommand(cmd, ctx);
	} catch(const MyError&) {
		return false;
	}

	return true;
}

bool ATUICommandManager::ExecuteCommandNT(const char *str, const ATUICommandOptions& ctx) noexcept {
	try {
		ExecuteCommand(str, ctx);
	} catch(const MyError&) {
		return false;
	}

	return true;
}

void ATUICommandManager::ListCommands(vdfastvector<VDAccelToCommandEntry>& commands) const {
	for(uint32 i=0; i<kHashTableSize; ++i) {
		for(const Node *node = mpHashTable[i]; node; node = node->mpNext) {
			VDAccelToCommandEntry& ace = commands.push_back();

			ace.mId = 0;
			ace.mpName = node->mpCmd->mpName;
		}
	}
}

void ATUICommandManager::NotifyCommandExecuted(const ATUICommandContext& ctx) {
	mOnCommandExecuted.InvokeAll(
		*ctx.mpCommand, ctx.mOutArgs
	);
}

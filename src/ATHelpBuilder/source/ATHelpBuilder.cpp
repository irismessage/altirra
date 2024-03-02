//	ATHelpBuilder - Altirra .CHM help file preprocessor
//	Copyright (C) 2010 Avery Lee
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

using namespace System;
using namespace System::Collections::Generic;
using namespace System::Text;
using namespace System::Xml;
using namespace System::Xml::Xsl;
using namespace System::IO;
using namespace System::Text::RegularExpressions;

ref class CommandLineParseException : Exception {
public:
	CommandLineParseException(String^ message)
		: Exception(message)
	{
	}
};

ref class BuildException : Exception {
public:
	BuildException(String^ message) : Exception(message) { }
};

ref class Program {
	String^ mHelpCompilerPath;
	String^ mSourcePath;
	String^ mOutputPath;
	Regex^ mXslInsnRegex;
	List<String^>^ mOutputFiles;
	Dictionary<String^, bool>^ mImages;

public:
	Program() {
		mOutputFiles = gcnew List<String^>();
		mImages = gcnew Dictionary<String^, bool>();
	}

	void Run(cli::array<String^>^ args) {
		try {
			RunInner(args);
		} catch (CommandLineParseException^ ex) {
			System::Console::WriteLine("Usage Error: " + ex->Message);

			PrintUsage();

			System::Environment::Exit(5);
		} catch (BuildException^ ex) {
			System::Console::WriteLine("Error: " + ex->Message);
			System::Environment::Exit(10);
		} catch (Exception^ ex) {
			System::Console::WriteLine("Fatal Error: " + ex->ToString());

			System::Environment::Exit(20);
		}
	}

private:
	static void PrintUsage()
	{
		System::Console::WriteLine(); 
		System::Console::WriteLine("Usage:");
		System::Console::WriteLine("  athelpbuilder [hcpath] [sourcepath] [outputpath]");
	}

	void RunInner(cli::array<String^>^ args)
	{
		if (args->Length < 3)
			throw gcnew CommandLineParseException("No help compiler path was specified.");

		mHelpCompilerPath = args[0];

		if (!File::Exists(mHelpCompilerPath))
			throw gcnew BuildException("Help compiler does not exist: " + mHelpCompilerPath);

		mSourcePath = args[1];

		if (!Directory::Exists(mSourcePath))
			throw gcnew BuildException("Source path does not exist: " + mHelpCompilerPath);

		mOutputPath = args[2];

		if (!Directory::Exists(mOutputPath))
		{
			Directory::CreateDirectory(mOutputPath);
		}

		mXslInsnRegex = gcnew Regex("type=\"text/xsl\" href=\"(.*)\"");

		for each(String^ files in Directory::GetFiles(mSourcePath, "*.xml")) {
			ProcessFile(files);
		}

		ProcessToc();

		mOutputFiles->Add("layout.css");

		{
			StreamWriter sw(Path::Combine(mOutputPath, "athelp.hhp"));
			{
				StreamReader sr(Path::Combine(mSourcePath, "athelp.hhp"));
				for(;;) {
					String^ s = sr.ReadLine();
					if (s == nullptr)
						break;

					sw.WriteLine(s);
				}
			}

			sw.WriteLine("[FILES]");

			for each(String^ file in mOutputFiles) {
				sw.WriteLine(file);
			}
		}

		String^ cssFile = Path::Combine(mSourcePath, "layout.css");
		String^ cssFileOut = Path::Combine(mOutputPath, "layout.css");
		if (File::Exists(cssFileOut))
			File::SetAttributes(cssFileOut, FileAttributes::Normal);
		File::Copy(cssFile, cssFileOut, true);
		File::SetAttributes(cssFile, FileAttributes::Normal);

		CopyImages();

		BuildHelpFile();
	}

	void ProcessToc()
	{
		XslCompiledTransform transform;
		transform.Load(Path::Combine(mSourcePath, "toc.xsl"));

		transform.Transform(Path::Combine(mSourcePath, "toc.xml"), Path::Combine(mOutputPath, "athelp.hhc"));
	}

	void ProcessFile(String^ file)
	{
		XmlDocument doc;
		String^ filename = Path::GetFileNameWithoutExtension(file);

		doc.Load(file);

		String^ xslname = nullptr;
		for each(XmlProcessingInstruction^ insn in doc.SelectNodes("processing-instruction()")) {
			Match^ match = mXslInsnRegex->Match(insn->Data);

			if (match->Success)
				xslname = match->Groups[1]->Captures[0]->Value;
		}

		if (xslname != nullptr) {
			String^ xslPath = Path::Combine(Path::GetDirectoryName(file), xslname);
			String^ resultFileName = filename + ".html";
			String^ resultPath = Path::Combine(mOutputPath, resultFileName);

			mOutputFiles->Add(resultFileName);

			System::Console::WriteLine("[xslt {0}] {1} -> {2}", xslPath, file, resultPath);

			XslCompiledTransform xslt;

			xslt.Load(xslPath);

			// transform to HTML
			{
				StreamWriter writer(resultPath);
				xslt.Transform(%doc, nullptr, %writer);
			}

			// transform to XML and scan for IMG tags
			{
				XmlDocument resultDoc;

				{
					XmlWriter^ writer = resultDoc.CreateNavigator()->AppendChild();
					xslt.Transform(%doc, nullptr, writer);
					writer->Close();
				}

				for each(XmlAttribute^ srcAttr in resultDoc.SelectNodes("//img[@src]/@src")) {
					String^ path = srcAttr->InnerText;

					if (!path->StartsWith("http:"))
						mImages->Add(path, false);
				}
			}	
		}
	}

	void CopyImages() {
		List<String^> images(mImages->Keys);
		images.Sort();

		for each(String^ s in images) {
			System::Console::WriteLine("[copying image] {0}", s);

			String^ relPath = s->Replace('/', '\\');
			String^ relDir = Path::GetDirectoryName(relPath);

			Directory::CreateDirectory(Path::Combine(mOutputPath, relDir));

			String^ outPath = Path::Combine(mOutputPath, relPath);
			File::Copy(Path::Combine(mSourcePath, relPath), outPath, true);
			File::SetAttributes(outPath, FileAttributes::Normal);
		}
	}

	void BuildHelpFile() {
		System::Diagnostics::ProcessStartInfo startInfo;

		startInfo.FileName = mHelpCompilerPath;
		startInfo.Arguments = Path::Combine(mOutputPath, "athelp.hhp");
		startInfo.UseShellExecute = false;

		System::Diagnostics::Process^ p = System::Diagnostics::Process::Start(%startInfo);

		p->WaitForExit();

		if (p->ExitCode == 0) {
			System::Console::WriteLine();
			throw gcnew BuildException("HTML Help compilation failed.");
		}
	}
};

int main(array<String^>^ args) {
	Program p;

	p.Run(args);
	return 0;
}

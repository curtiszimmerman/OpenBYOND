/*
OpenBYOND DMScript Preprocessor

Originally written for DreamCatcher by nan0desu, significantly updated and 
changed to support full DM parsing.

Copyright (c) 2014 Rob "N3X15" Nelson

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/
#include "scripting/Preprocessor.h"
#include "string_utils.h"
#include "vector_utils.h"
#include <vector>
#include <string>
#include <cstdarg>
#include <sstream>
#include <fstream>
#include <iostream>
#include <stack>

IgnoreState::IgnoreState(std::string start, bool ignoring, ...):
	starttoken(start),
	ignoring(ignoring),
	endtokens()
{
	int n;
	const char *endTokenC;
	std::string endToken;
	va_list vl;
	va_start(vl,ignoring);
	while((endTokenC=va_arg(vl,const char*)) != NULL)
	{
		endToken=std::string(endTokenC);
		endtokens.push_back(endToken);
	}
	va_end(vl);
};

Preprocessor::Preprocessor():
	defines()
{
	//
}

Preprocessor::~Preprocessor() {;}

void Preprocessor::rewindBuffer(std::string &buf, int numchars) {
	buf=buf.substr(0,buf.length()-(numchars+1));
}
void Preprocessor::rewindStream(std::iostream &stream, int numchars) {
	stream.seekg(-numchars,stream.cur);
}
	
std::string Preprocessor::ParseFile(std::string filename) {
	std::fstream fin(filename, std::fstream::in);
	if (fin==NULL)
	{
		return "";
	}
	std::fstream fout(filename+".tmp", std::fstream::out);
	if (fout==NULL)
	{
		fin.close();
		return "";
	}
	
	ParseStream(fin,fout,filename);
	
	fin.close();
	fout.close();
	return filename+".tmp";
}

void Preprocessor::ParseStream(std::iostream &fin, std::iostream &fout, std::string streamname) {
	fout << "/// OpenBYOND Preprocessed Code\r\n";
	fout << "/// File: " << streamname << "\r\n";
	fout << "///\r\n\r\n";
	// Parse char by char
	char c;
	char lastchar=0;
	char nextlastchar=0;
	std::string buf;
	bool escape=false;
	bool encounteredOtherCharacters = false;
	int ignorelevel = 0;
	int line=1;
	std::string token("");
	std::stack<std::string> endtokens;
	bool skipNextChar=false;
	while (fin >> std::noskipws >> c) {
		// Skip windows return characters.
		token="";
		if(nextlastchar)
			lastchar = nextlastchar;
			token += lastchar;
		token += c;
		nextlastchar=c;
		if(c == '\r') continue;
		if(c == '\n' && !escape) {
			line++;
			while(hasEnding(buf,"\n")||hasEnding(buf,"\r")) {
				buf = trim(buf," \t\r\n");
			}
			if(!IsIgnoring() && endtokens.size()==0) {
				printf("LINE ");
				std::string _c;
				for(int i = 0;i<buf.length();i++) {
					_c=buf.substr(i,1);
					if(_c=="\n" || _c=="\r") continue;
					printf("%s",_c.c_str());
				}
				printf("\n");
				fout << buf;// << "\r\n";
			}
			buf = "";
			encounteredOtherCharacters=false;
		} else if(c == '\\') {
			escape=true;
			continue;
		} else if(c == '#') {
			if(endtokens.size()>0) continue;
			if(encounteredOtherCharacters && ignorelevel == 0){
				printf("%s:%d [PP] ERROR:  Encountered non-whitespace characters before preprocessor token!",streamname.c_str(),line);
				return;
			}
			consumePPToken(fin,fout);
			lastchar=0;
			continue;
		} else if(c == '"' || c == '\'') {
			std::stringstream o("");
			o << c;
			if(c=='"' && lastchar == '{')
				consumeUntil(fin,o,"\"}");
			else
				consumeUntil(fin,o,c);
			o << c;
			if(!IsIgnoring() && endtokens.size()==0) 
				buf += o.str();
			continue;
		} else if(lastchar == '/' && c == '/') {
			std::stringstream devnull("");
			consumeUntil(fin,devnull,'\n');
			printf("Discarded \"//%s\"\n",devnull.str().c_str());
			rewindBuffer(buf,1);
			rewindStream(fin,1);
			encounteredOtherCharacters=false;
			lastchar=0;
			continue;
		} else {
			if(c!='\t'&&c!=' ')
				encounteredOtherCharacters=true;
		}
		//printf("TOKEN %s\n",token.c_str());
		if(token=="/*") {
			endtokens.push("*/");
			rewindBuffer(buf,1);
			//printf("BEGIN COMMENT\n");
			//printf("START BUFFER %s\n",buf.c_str());
			continue;
		} else {
			if(endtokens.size()>0 && endtokens.top() == token){
				endtokens.pop();
				rewindBuffer(buf,1);
				//printf("END COMMENT\n");
				//printf("END BUFFER %s\n",buf.c_str());
				continue;
			}
		}
		if(endtokens.size()>0)
			continue;
		buf += c; // Or whatever
	}
}
	
void Preprocessor::consumePreprocessorToken(std::string token,std::vector<std::string> args)
{
	if(token == "ifdef")  consumeIfdef(args);
	if(token == "else")   consumeElse(args);
	if(token == "endif")  consumeEndif(args);
	if(token == "define") consumeDefine(args);
	if(token == "undef")  consumeUndef(args);
}

bool Preprocessor::IsIgnoring() {
	std::vector<IgnoreState>::iterator it;
	IgnoreState st8;
	for(it=ignoreStack.begin();it!=ignoreStack.end();it++) {
		st8 = *it;
		if(st8.ignoring) return true;
	}
	return false;
}
void Preprocessor::consumeIfdef(std::vector<std::string> args){
	std::map<std::string,std::string>::iterator it;
	std::string defname = args[0];
	it = defines.find(defname);
	ignoreStack.push_back(IgnoreState("ifdef",it != defines.end(),"else","endif",NULL));
}

void Preprocessor::consumeIfndef(std::vector<std::string> args){
	std::map<std::string,std::string>::iterator it;
	it = defines.find(args[0]);
	ignoreStack.push_back(IgnoreState("ifndef",it == defines.end(),"else","endif",NULL));
}

void Preprocessor::consumeElse(std::vector<std::string> args) {
	IgnoreState st8 = ignoreStack.back();
	ignoreStack.pop_back();
	ignoreStack.push_back(IgnoreState("else",!st8.ignoring,"endif"));
}
void Preprocessor::consumeEndif(std::vector<std::string> args){
	ignoreStack.pop_back();
}

// #define
void Preprocessor::consumeDefine(std::vector<std::string> args){
	if(args[0].find("(")){
		std::cerr << "[PP] ERROR: OpenBYOND does not currently support preprocessor macros." << std::endl; 
		return;
	}
	std::string imploded;
	implode(VectorCopy<std::string>(args,1), ' ', imploded);
	defines[args[0]]=imploded;
}

// #undef
void Preprocessor::consumeUndef(std::vector<std::string> args){
	std::map<std::string,std::string>::iterator it;
	it = defines.find(args[0]);
	if (it == defines.end()) return;
	defines.erase(it);
}

void Preprocessor::consumeUntil(std::iostream &fin, std::iostream &fout, char endmarker) {
	char c;
	while (fin >> std::noskipws >> c) {
		if(c==endmarker) return;
		if(fout!=NULL) {
			fout << c;
		}
	}
}
void Preprocessor::consumeUntil(std::iostream &fin, std::iostream &fout, std::string endtoken) {
	char c;
	char lastchar='\0';
	std::string token;
	while (fin >> std::noskipws >> c) {
		token="";
		if(lastchar)
			token += lastchar;
		token += c;
		lastchar=c;
		if(token==endtoken) return;
		if(fout!=NULL) {
			fout << c;
		}
	}
}

void Preprocessor::consumePPToken(std::iostream &fin, std::iostream &fout) {
	std::stringstream buf;
	consumeUntil(fin,buf,'\n');
	std::string line = buf.str();
	line=trim(line," \t\r\n");
	std::vector<std::string> args = split(line,' ');
	std::string token = args[0];
	args=VectorCopy<std::string>(args,1);
	
	fout << "\n/* Found #" << token;
	
	consumePreprocessorToken(token,args);
	
	fout << " (ignore:" << (this->IsIgnoring()?"y":"n") << "). */";
}
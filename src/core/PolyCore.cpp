/*
 Copyright (C) 2011 by Ivan Safrin
 
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

#include "polycode/core/PolyGlobals.h"
#include "polycode/core/PolyCore.h"
#include "polycode/core/PolyCoreInput.h"
#include "polycode/core/PolyRenderer.h"
#include "polycode/core/PolyLogger.h"
#include "polycode/core/PolyCoreServices.h"
#include "polycode/core/PolyBasicFileProvider.h"

#if PLATFORM == PLATFORM_WINDOWS
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <time.h>

namespace Polycode {

	TimeInfo::TimeInfo() {
		time_t rawtime;
		struct tm * timeinfo;
		
		time( &rawtime );
		timeinfo = localtime ( &rawtime );
	
		seconds = timeinfo->tm_sec;
		minutes = timeinfo->tm_min;
		hours = timeinfo->tm_hour;
		month = timeinfo->tm_mon;
		monthDay = timeinfo->tm_mday;
		weekDay = timeinfo->tm_wday;
		year = timeinfo->tm_year;
		yearDay = timeinfo->tm_yday;
	}
	
	Core::Core(int _xRes, int _yRes, bool fullScreen, bool vSync, int aaLevel, int anisotropyLevel, int frameRate, int monitorIndex) : EventDispatcher() {
		
		CoreServices::createInstance();
	
		int _hz;
		getScreenInfo(&defaultScreenWidth, &defaultScreenHeight, &_hz);
	
		coreResized = false;
		
		this->aaLevel = aaLevel;
		this->anisotropyLevel = anisotropyLevel;
		services = CoreServices::getInstance();
		input = new CoreInput();
		services->setCore(this);
		fps = 0;
		timeLeftOver = 0.0;
		running = true;
		frames = 0;
		lastFrameTicks=0;
		lastFPSTicks=0;
		elapsed = 0;
		xRes = _xRes;
		yRes = _yRes;
		this->vSync = vSync;
		paused = false;
		pauseOnLoseFocus = false;
		if (fullScreen && !xRes && !yRes) {
			getScreenInfo(&xRes, &yRes, NULL);
		}
		mouseEnabled = true; mouseCaptured = false;
		lastSleepFrameTicks = 0;
		
		this->monitorIndex = monitorIndex;
		
		if(frameRate == 0)
			frameRate = 60;
		
		setFramerate(frameRate);
		threadedEventMutex = NULL;
		
	}
	
	void Core::setVideoMode(int xRes, int yRes, bool fullScreen, bool vSync, int aaLevel, int anisotropyLevel, bool retinaSupport) {
		
		VideoModeChangeInfo *modeChangeInfo =  new VideoModeChangeInfo();
		
		modeChangeInfo->retinaSupport = retinaSupport;
		modeChangeInfo->xRes = xRes;
		modeChangeInfo->yRes = yRes;
		modeChangeInfo->fullScreen = fullScreen;
		modeChangeInfo->vSync = vSync;
		modeChangeInfo->aaLevel = aaLevel;
		modeChangeInfo->anisotropyLevel = anisotropyLevel;

		renderer->getRenderThread()->enqueueJob(RenderThread::JOB_REQUEST_CONTEXT_CHANGE, modeChangeInfo);
	}
	
	
	int Core::getScreenWidth() {
		int width, height, hz;
		getScreenInfo(&width, &height, &hz);
		return width;
	}
	
	int Core::getScreenHeight() {
		int width, height, hz;
		getScreenInfo(&width, &height, &hz);
		return height;
	}
	
	void Core::setFramerate(int frameRate, int maxFixedCycles) {
		refreshInterval = 1000 / frameRate;
		fixedTimestep = 1.0 / ((double) frameRate);
		maxFixedElapsed = fixedTimestep * maxFixedCycles;
	}
	
	void Core::enableMouse(bool newval) {
		mouseEnabled = newval;
	}

	void Core::captureMouse(bool newval) {
		mouseCaptured = newval;
	}
		
	Number Core::getXRes() {
		return xRes;
	}

	Number Core::getYRes() {
		return yRes;
	}
	
	CoreInput *Core::getInput() {
		return input;
	}	
	
	Core::~Core() {
		delete services;
		delete input;
	}
	
	void Core::Shutdown() { 
		running = false;
	}
	
	String Core::getUserHomeDirectory() {
		return userHomeDirectory;
	}	
	
	String Core::getDefaultWorkingDirectory() {
		return defaultWorkingDirectory;
	}
	
	Number Core::getElapsed() {
		return ((Number)elapsed)/1000.0f;
	}
	
	double Core::getTicksFloat() {
		return getTicks()/1000.0;
	}
		
	void Core::createThread(Threaded *target) {
		if(!threadedEventMutex) {
			threadedEventMutex = createMutex();
		}
		target->eventMutex = threadedEventMutex;
		target->core = this;
		
		lockMutex(threadedEventMutex);
		threads.push_back(target);
		unlockMutex(threadedEventMutex);			
	}
	
	CoreMutex *Core::getEventMutex() {
		return eventMutex;
	}

	void Core::resizeTo(int xRes, int yRes) {
		this->xRes = xRes;
		this->yRes = yRes;
		coreResized = true;
		Render();
	}
	
	void Core::loseFocus() {
		if(pauseOnLoseFocus) {
			paused = true;
		}
		input->clearInput();
		dispatchEvent(new Event(), EVENT_LOST_FOCUS);
	}
	
	void Core::gainFocus() {
		if(pauseOnLoseFocus) {
			paused = false;
		}	
		input->clearInput();		
		dispatchEvent(new Event(), EVENT_GAINED_FOCUS);
	}
	
	void Core::removeThread(Threaded *thread) {
		if(threadedEventMutex){ 
			lockMutex(threadedEventMutex);
	
			for(int i=0; i < threads.size(); i++) {
				if(threads[i] == thread) {
					threads.erase(threads.begin() + i);
					return;
				}
			}
			unlockMutex(threadedEventMutex);			
		}
	}
	
	bool Core::updateAndRender() {
		bool ret = Update();
		Render();
		return ret;
	}
	
	bool Core::fixedUpdate() {
		if(fixedElapsed < fixedTimestep) {
			return false;
		}
		services->fixedUpdate();
		fixedElapsed -= fixedTimestep;
		return true;
	}
	
	Number Core::getFixedTimestep() {
		return fixedTimestep;
	}
	
	bool Core::Update() {
		bool ret = systemUpdate();
		while(fixedUpdate()) {}
		return ret;
	}
							
	void Core::updateCore() {
		frames++;
		frameTicks = getTicks();
		elapsed = frameTicks - lastFrameTicks;
			  
		if(elapsed > 1000)
			elapsed = 1000;

		if(fixedElapsed > 0) {
			timeLeftOver = fixedElapsed;
		} else {
			timeLeftOver = 0;
		}
		
		fixedElapsed = (((Number)elapsed)/1000.0f) + timeLeftOver;
		
		if(fixedElapsed > maxFixedElapsed) {
			fixedElapsed = maxFixedElapsed;
		}
		
		if(coreResized) {
			coreResized = false;
			dispatchEvent(new Event(), EVENT_CORE_RESIZE);
		}
		
		services->Update(elapsed);
		
		if(frameTicks-lastFPSTicks >= 1000) {
			fps = frames;
			frames = 0;
			lastFPSTicks = frameTicks;
		}
		lastFrameTicks = frameTicks;
		
		if(threadedEventMutex){ 
		lockMutex(threadedEventMutex);

		std::vector<Threaded*>::iterator iter = threads.begin();
		while (iter != threads.end()) {		
			for(int j=0; j < (*iter)->eventQueue.size(); j++) {
				Event *event = (*iter)->eventQueue[j];
				(*iter)->__dispatchEvent(event, event->getEventCode());
				if(event->deleteOnDispatch)
					delete event;
			}
			(*iter)->eventQueue.clear();
			if((*iter)->scheduledForRemoval) {
				iter = threads.erase(iter);
			} else {
				++iter;
			}
		}
		
		unlockMutex(threadedEventMutex);
		}
	}
	
	void Core::doSleep() {
		unsigned int ticks = getTicks();
		unsigned int ticksSinceLastFrame = ticks - lastSleepFrameTicks;
		int sleepTimeMs = refreshInterval - ticksSinceLastFrame;
		if(sleepTimeMs > 0) {
#if PLATFORM == PLATFORM_WINDOWS
			Sleep(sleepTimeMs);
#else
			usleep(sleepTimeMs * 1000);
#endif
		}
		lastSleepFrameTicks = getTicks();
		timeSleptMs = lastSleepFrameTicks - ticks;
	}
	
	
	Number Core::getFPS() {
		return fps;
	}
	
	CoreServices *Core::getServices() {
		return services;
	}
	
	CoreFile *Core::openFile(const Polycode::String& fileName, const Polycode::String& opts) {
		for(int i=0; i < fileProviders.size(); i++) {
			CoreFile *file = fileProviders[i]->openFile(fileName, opts);
			if(file) {
				file->provider = fileProviders[i];
				return file;
			}
		}
		return NULL;
	}
	
	void Core::closeFile(CoreFile *file) {
		for(int i=0; i < fileProviders.size(); i++) {
			if(file->provider == fileProviders[i]) {
				fileProviders[i]->closeFile(file);
				return;
			}
		}
		assert(false); // CLOSING A FILE FOR A NON-EXISTING PROVIDER
	}
	
	void Core::addFileSource(const String &type, const String &source) {
		for(int i=0; i < fileProviders.size(); i++) {
			if(fileProviders[i]->type == type) {
				fileProviders[i]->addSource(source);
				return;
			}
		}
	}
	
	String Core::getResourcePathForFile(const String &fileName) {
		return fileName;
	}
	
	void Core::removeFileSource(const String &type, const String &source) {
		for(int i=0; i < fileProviders.size(); i++) {
			if(fileProviders[i]->type == type) {
				fileProviders[i]->removeSource(source);
				return;
			}
		}
	}
	
	std::vector<OSFileEntry> Core::parseFolder(const Polycode::String& pathString, bool showHidden) {
		std::vector<OSFileEntry> retVec;
		
		for(int i=0; i < fileProviders.size(); i++) {
			if(fileProviders[i]->canListFiles) {
				if(fileProviders[i]->parseFolder(pathString, showHidden, retVec)) {
					return retVec;
				}
			}
		}
		
		systemParseFolder(pathString, showHidden, retVec);		  
		return retVec;
	}

	void Core::lockMutex(CoreMutex *mutex) {
		mutex->lock();
	}


	void Core::unlockMutex(CoreMutex *mutex) {
		mutex->unlock();
	}
	
	DummyCore::DummyCore(): Core(0, 0, false, false, 0, 0, 0, 0) {
		fileProviders.push_back(new BasicFileProvider());
	}
	
	DummyCore::~DummyCore() {}
	void DummyCore::Render() {}
	bool DummyCore::systemUpdate()	{
		if(!running)
			return false;
		doSleep();
		updateCore();
		return running;
	}
	void DummyCore::setCursor(int cursorType) {}
	void DummyCore::createThread(Threaded *target) {}
	CoreMutex *DummyCore::createMutex() { return NULL; }
	void DummyCore::copyStringToClipboard(const String& str) {}
	String DummyCore::getClipboardString() { return ""; }
	void DummyCore::createFolder(const String& folderPath) {}
	void DummyCore::copyDiskItem(const String& itemPath, const String& destItemPath) {}
	void DummyCore::moveDiskItem(const String& itemPath, const String& destItemPath) {}
	void DummyCore::removeDiskItem(const String& itemPath) {}
	String DummyCore::openFolderPicker() { return "";}
	std::vector<String> DummyCore::openFilePicker(std::vector<CoreFileExtension> extensions, bool allowMultiple) {
		std::vector<String> ret;
		return ret;
	}
	String DummyCore::saveFilePicker(std::vector<CoreFileExtension> extensions) {
		return "";
	}
	void DummyCore::handleVideoModeChange(VideoModeChangeInfo *modeInfo) {}
	void DummyCore::flushRenderContext() {}
	void DummyCore::openURL(String url) {}
	unsigned int DummyCore::getTicks() { return 0;}
	String DummyCore::executeExternalCommand(String command, String args, String inDirectory) {
		return "";
	}
	bool DummyCore::systemParseFolder(const Polycode::String& pathString, bool showHidden, std::vector<OSFileEntry> &targetVector) {
		return false;
	}
}

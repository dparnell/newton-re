/*
	File:		sound/SoundServer.h

	Contains:	The sound server: TSoundServer, the 'sndm' app world that
				owns the sound hardware (sound/SoundDriver.h), and the
				channels a client opens on it (sound/SoundChannel.h is the
				client's side).

				A client talks to the server with 'newt'/'usnd' events: an
				immediate RPC for each command (open, close, start, pause,
				stop, the volumes), and for each block of sound it wants
				played a TUSoundNodeRequest whose reply comes back only
				when the block has been played - which is how the client
				learns a sound has finished.  The server keeps each
				channel's blocks as a list of ChannelNodes; an output
				channel (TDMAChannel) turns its current node's samples
				into the hardware's format and rate (Produce, over
				sound/Resample.h and sound/SampleConvert.h) straight into
				the DMA buffer the driver will play next.

				The driver double-buffers: two buffers of 0xea0 bytes, one
				playing while the other is filled.  When one has been
				played the driver interrupts, SoundOutputIH sends the
				server a message (event 4), and the server fills that
				buffer again with every running output channel mixed in
				(FillDMABuffer, MixLin16) and hands it back to the driver
				- or, when nothing is left to play, turns the output off.

				A decompressor channel (TCodecChannel) sits in front of an
				output channel for sound that has to go through a codec
				first; its own decompressing task is NOT YET (below).

	NOT YET RECONSTRUCTED: sound input and the compressor channels
	(OpenInputChannel, StartInput, EmptyDMABuffer, SoundInputIH,
	OpenCompressorChannel, CompressLoop - the Sound Recorder's recording),
	TCodecChannel's task and loops (MainEventLoop, DecompressLoop,
	FillDMABuffer, InitCodecNodes, ...: a codec's sound is not played -
	sound/SoundChannel.h's TFrameSoundChannel says so), and
	TSoundPowerHandler (the hardware powered down on a power-off event).

	Reconstructed from the MP2x00 US ROM (0x001e36fc-0x001e6170,
	0x001e7380, 0x001e7ef0-0x001e99f0); each function cites its origin.
*/

#ifndef __SOUNDSERVER_H
#define __SOUNDSERVER_H

#ifndef __APPWORLD_H
#include "AppWorld.h"
#endif

#ifndef __SOUNDDRIVER_H
#include "SoundDriver.h"
#endif

#ifndef __RESAMPLE_H
#include "Resample.h"
#endif

#include "UserPorts.h"


// the 'usnd' commands (the server's AEHandlerProc; TUSoundChannel sends them)
enum
{
	kSndStopAll				= 1,
	kSndOutputDone			= 4,		// the output interrupt's message
	kSndInputDone			= 5,
	kSndOpenOutput			= 6,
	kSndOpenInput			= 7,
	kSndClose				= 8,
	kSndStart				= 9,		// start and answer at once
	kSndStartWait			= 10,		// start and answer when the channel has played everything
	kSndPause				= 11,
	kSndStop				= 12,
	kSndSchedule			= 13,
	kSndCancel				= 14,
	kSndSetVolume			= 15,
	kSndSetInputGain		= 16,
	kSndSetInputDevice		= 17,
	kSndSetOutputDevice		= 18,
	kSndOpenCompressor		= 19,
	kSndOpenDecompressor	= 20,
	kSndGetVolume			= 21
};

// the server's errors (ERRBASE_SOUND is -30000)
enum
{
	kSndErrGeneric			= ERRBASE_SOUND - 2,		// -30002: an unknown command
	kSndErrNoChannel		= ERRBASE_SOUND - 10,		// -30010: no such channel, or not open
	kSndErrNothingToPlay	= ERRBASE_SOUND - 8,		// -30008: no node scheduled
	kSndErrBadFormat		= ERRBASE_SOUND - 9,		// -30009: a coding the channel cannot play
	kSndErrCancelled		= ERRBASE_SOUND - 11,		// -30011: the node was cancelled
	kSndErrStopped			= ERRBASE_SOUND - 13,		// -30013: the node was stopped
	kSndErrNoDriver			= ERRBASE_SOUND - 16		// -30016: no sound hardware
};

// A block of sound, as a client describes it (0x34 bytes in the ROM).
struct SoundBlock
{
	void*		fData;				// +0x00  the samples (or the codec's coded data)
	long		fCount;				// +0x04  how many samples there are
	long		fSampleBits;		// +0x08  8 or 16
	long		fFormat;			// +0x0c  kSoundFormatStd8, kSoundFormatMuLaw, kSoundFormatLinear16
	Fixed		fSampleRate;		// +0x10  samples a second, 16.16
	long		fVolume;			// +0x14  16.16 decibels (0x7fffffff: the channel's)
	long		fStart;				// +0x18  the first sample to play
	long		fPlayCount;			// +0x1c  how many to play
	long		fLoops;				// +0x20  times more to play it
	void*		fRefCon;			// +0x24  the client's (TFrameSoundChannel: the sound frame)
	void*		fCodec;				// +0x28  a TSoundCodec, nil for plain samples
	long		fUnknown2C;			// +0x2c
	long		fUnknown30;			// +0x30
};

// A request to play a block (TUSoundChannel::Schedule sends it; 0x4c bytes).
struct TUSoundNodeRequest
{
	TAEvent		fEvent;				// +0x00  'newt'/'usnd'
	ULong		fChannel;			// +0x08
	ULong		fCommand;			// +0x0c  kSndSchedule
	ULong		fNodeId;			// +0x10
	SoundBlock	fBlock;				// +0x14
	long		fChannelVolume;		// +0x48
};

// What the server answers about a node: when it is done (the request's
// reply) or where a pause or stop left it (0x20 bytes).
struct TUSoundNodeReply
{
	TAEvent		fEvent;				// +0x00
	ULong		fChannel;			// +0x08
	long		fError;				// +0x0c
	ULong		fUnknown10;			// +0x10
	ULong		fNodeId;			// +0x14
	long		fState;				// +0x18  FreeNode's flag; 1 stopped, 2 paused
	long		fPosition;			// +0x1c  samples played
};

// A block the server is playing (0x5c bytes in the ROM).
struct ChannelNode
{
	ChannelNode*	fNext;			// +0x00
	ULong			fFlags;			// +0x04  1: counted among the filtered nodes
	ULong			fNodeId;		// +0x08
	long			fPosition;		// +0x0c  samples played of fPlayCount
	void*			fData;			// +0x10  the request's block, from here on
	long			fCount;			// +0x14
	long			fSampleBits;	// +0x18
	long			fFormat;		// +0x1c
	Fixed			fSampleRate;	// +0x20
	long			fVolume;		// +0x24
	long			fStart;			// +0x28
	long			fPlayCount;		// +0x2c
	long			fLoops;			// +0x30
	void*			fRefCon;		// +0x34
	void*			fCodec;			// +0x38
	long			fUnknown3C;		// +0x3c
	long			fUnknown40;		// +0x40
	TUMsgToken		fToken;			// +0x44  the request's, answered when the node is freed
	long			fChannelVolume;	// +0x54
	ULong			fUnknown58;		// +0x58
};


/*------------------------------------------------------------------------------
	T S o u n d C h a n n e l
	What the server keeps of a channel.  The virtuals are in the ROM's
	slot order (analysis/vtable.py on TDMAChannel's table at 0x0001b1b4).
------------------------------------------------------------------------------*/

enum
{
	kSndChannelOutput		= 0x01,
	kSndChannelInput		= 0x02,
	kSndChannelRunning		= 0x04,
	kSndChannelPaused		= 0x08,
	kSndChannelCompressor	= 0x10,
	kSndChannelDecompressor	= 0x20,
	kSndChannelKeepRunning	= 0x40,
	kSndChannelFixedVolume	= 0x80		// the gestalt says the hardware's volume is fixed
};

class TSoundChannel
{
public:
						TSoundChannel(ULong id);						// ROM 0x001e36fc __ct__13TSoundChannelFUl
	virtual				~TSoundChannel();								// ROM 0x001e3764 __dt__13TSoundChannelFv
	virtual long		Schedule(TUSoundNodeRequest* request, TUMsgToken* token);	// ROM 0x001e42ec Schedule__13TSoundChannelFP18TUSoundNodeRequestP10TUMsgToken
	virtual long		Cancel(TUSoundNodeRequest* request);			// ROM 0x001e464c Cancel__13TSoundChannelFP18TUSoundNodeRequest
	virtual long		Start(TUMsgToken* token);						// ROM 0x001e4eec Start__13TSoundChannelFP10TUMsgToken
	virtual long		Pause(TUSoundNodeReply* reply);					// ROM 0x001e5b78 Pause__13TSoundChannelFP16TUSoundNodeReply
	virtual void		Stop(TUSoundNodeReply* reply, long error);		// ROM 0x001e5e80 Stop__13TSoundChannelFP16TUSoundNodeReplyl
	virtual long		FreeNode(ChannelNode* node, long error, int state);	// ROM 0x001e5fb8 FreeNode__13TSoundChannelFP11ChannelNodeli
	virtual void		SetupNode(ChannelNode* node) = 0;				// (+0x1c)
	virtual void		CleanupNode(ChannelNode* node);					// ROM 0x001e60d0 CleanupNode__13TSoundChannelFP11ChannelNode

	NewtonErr			MakeNode(ChannelNode** node);					// ROM 0x001e5f34 MakeNode__13TSoundChannelFPP11ChannelNode
	Boolean				IsActive(void) const			// running, with a node, and not paused
						{ return (fFlags & kSndChannelRunning) != 0 && fNodes != nil && (fFlags & kSndChannelPaused) == 0; }

	ULong				fId;			// +0x04
	TSoundChannel*		fNext;			// +0x08  the server's list
	ChannelNode*		fNodes;			// +0x0c  the one playing first
	ChannelNode*		fFreeNodes;		// +0x10
	ULong				fFlags;			// +0x14
	ULong				fHaveStartToken;	// +0x18
	TUMsgToken			fStartToken;	// +0x1c  a kSndStartWait's, answered when the channel runs dry
	ULong				fDevice;		// +0x2c
	Boolean				fResampleDirty;	// +0x30
	ResampleState		fResampleState;	// +0x34
};


class TDMAChannel : public TSoundChannel
{
public:
						TDMAChannel(ULong id, const TSoundDriverInfo& info);	// ROM 0x001e3804 __ct__11TDMAChannelFUlRC16TSoundDriverInfo
	virtual				~TDMAChannel();											// ROM 0x001e38e4 __dt__11TDMAChannelFv
	virtual void		SetupNode(ChannelNode* node);							// ROM 0x001e3948 SetupNode__11TDMAChannelFP11ChannelNode

	long				GetVolume(void);										// ROM 0x001e3924 GetVolume__11TDMAChannelFv
	Boolean				Prep(void);												// ROM 0x001e3bbc Prep__11TDMAChannelFv
	long				Produce(void* buffer, long* count);						// ROM 0x001e3c74 Produce__11TDMAChannelFPvPl
	long				Consume(void* buffer, long* count);						// ROM 0x001e3ec4 Consume__11TDMAChannelFPvPl

	void				(*fFilteredResample)(ResampleState*, long*, long*);	// +0x1dc
	void				(*fResampleProc)(const SampleSpec*, long*, long*);	// +0x1e0
	void				(*fSampleConvert)(void*, void*);					// +0x1e4
	void				(*fBlockConvert)(void*, long*, void*, long*);		// +0x1e8
	long				fHardwareFormat;		// +0x1ec
	long				fHardwareBits;			// +0x1f0
	long				fHardwareRate;			// +0x1f4  samples a second
	long				fNodeBits;				// +0x1f8
	long				fNodeRate;				// +0x1fc
	long				fNodeVolume;			// +0x200
	long				fChannelVolume;			// +0x204
};


class TCodecChannel : public TSoundChannel
{
public:
						TCodecChannel(ULong id, const TSoundDriverInfo& info);	// ROM 0x001e4110 __ct__13TCodecChannelFUlRC16TSoundDriverInfo
	virtual				~TCodecChannel();										// ROM 0x001e419c __dt__13TCodecChannelFv
	virtual void		SetupNode(ChannelNode* node);							// ROM 0x001e442c SetupNode__13TCodecChannelFP11ChannelNode

	ChannelNode*		fCodecNodes;			// +0x1e0  (NOT YET: the decompressing loop's nodes)
	ULong				fUnknown1E4;			// +0x1e4
	TULockingSemaphore	fLock;					// +0x1e8
	ULong				fUnknown1F4;			// +0x1f4
	TUPort				fPort;					// +0x1f8  its task's (NOT YET: the task)
	ULong				fOutputChannelId;		// +0x200  the output channel it feeds
	TSoundChannel*		fOutputChannel;			// +0x204
	ULong				fUnknown208;			// +0x208
};


/*------------------------------------------------------------------------------
	T S o u n d S e r v e r
------------------------------------------------------------------------------*/

class TSoundServer;

class TSoundServerHandler : public TAEventHandler
{
public:
	long				Init(TSoundServer* server);						// ROM 0x001e7ef0 Init__19TSoundServerHandlerFP12TSoundServer
	virtual void		AEHandlerProc(TUMsgToken* token, ULong* size, TAEvent* event);	// ROM 0x001e7f08 AEHandlerProc__19TSoundServerHandlerFP10TUMsgTokenPUlP7TAEvent

	TSoundServer*		fServer;		// +0x14
};

// The event an interrupt handler sends the server (0x14 bytes of content).
struct TSoundIntEvent
{
	TAEvent				fEvent;			// +0x00  'newt'/'usnd'
	ULong				fCount;			// +0x08  interrupts not yet answered
	ULong				fCommand;		// +0x0c  kSndOutputDone or kSndInputDone
	ULong				fUnknown10;		// +0x10
};

struct TSoundIntMessage					// 0x24 bytes in the ROM
{
	TUAsyncMessage		fMessage;
	TSoundIntEvent		fEvent;
};

class TSoundServer : public TAppWorld
{
public:
						TSoundServer();											// ROM 0x001e81cc __ct__12TSoundServerFv
	virtual ULong		GetSizeOf();											// ROM 0x001e86c4 GetSizeOf__12TSoundServerFv
	virtual long		MainConstructor();										// ROM 0x001e8274 MainConstructor__12TSoundServerFv
	virtual void		TheMain();												// ROM 0x001e8690 TheMain__12TSoundServerFv

	long				CloseChannel(ULong id);									// ROM 0x001e86cc CloseChannel__12TSoundServerFUl
	long				StartChannel(ULong id, TUMsgToken* token);				// ROM 0x001e8818 StartChannel__12TSoundServerFUlP10TUMsgToken
	long				PauseChannel(ULong id, TUSoundNodeReply* reply);		// ROM 0x001e8944 PauseChannel__12TSoundServerFUlP16TUSoundNodeReply
	long				StopChannel(ULong id, TUSoundNodeReply* reply);			// ROM 0x001e8aac StopChannel__12TSoundServerFUlP16TUSoundNodeReply
	long				ScheduleNode(TUSoundNodeRequest* request, TUMsgToken* token);	// ROM 0x001e8b70 ScheduleNode__12TSoundServerFP18TUSoundNodeRequestP10TUMsgToken
	long				CancelNode(TUSoundNodeRequest* request);				// ROM 0x001e8bb0 CancelNode__12TSoundServerFP18TUSoundNodeRequest
	TSoundChannel*		FindChannel(ULong id);									// ROM 0x001e8be8 FindChannel__12TSoundServerFUl
	void				StopAll(void);											// ROM 0x001e8ca0 StopAll__12TSoundServerFv
	ULong				UniqueId(void);											// ROM 0x001e8ce0 UniqueId__12TSoundServerFv
	NewtonErr			OpenOutputChannel(ULong* id, ULong device);				// ROM 0x001e8d20 OpenOutputChannel__12TSoundServerFPUlUl
	Boolean				AllOutputChannelsEmpty(void);							// ROM 0x001e8dec AllOutputChannelsEmpty__12TSoundServerFv
	void				StartOutput(long device);								// ROM 0x001e8e2c StartOutput__12TSoundServerFi
	void				StopOutput(long hardware);								// ROM 0x001e8ef4 StopOutput__12TSoundServerFi
	void				ScheduleOutputBuffer(void);								// ROM 0x001e8f60 ScheduleOutputBuffer__12TSoundServerFv
	Boolean				PrepOutputChannels(void);								// ROM 0x001e8fdc PrepOutputChannels__12TSoundServerFv
	ULong				FillDMABuffer(void);									// ROM 0x001e901c FillDMABuffer__12TSoundServerFv
	long				SetOutputVolume(long decibels);							// ROM 0x001e9568 SetOutputVolume__12TSoundServerFl
	static long			SoundOutputIH(void* server);							// ROM 0x001e95a8 SoundOutputIH__12TSoundServerFv
	long				SetOutputDevice(ULong id, long device);					// ROM 0x001e9694 SetOutputDevice__12TSoundServerFUll
	NewtonErr			OpenDecompressorChannel(ULong* id, ULong outputId);		// ROM 0x001e96b4 OpenDecompressorChannel__12TSoundServerFPUlUl
	void				StartDecompressor(long device);							// ROM 0x001e9790 StartDecompressor__12TSoundServerFi
	void				StopDecompressor(long hardware);						// ROM 0x001e9794 StopDecompressor__12TSoundServerFi

	TSoundIntMessage*	fOutputIntMessage;		// +0x70
	TSoundIntMessage*	fInputIntMessage;		// +0x74
	ULong				fLastId;				// +0x78
	TSoundServerHandler	fHandler;				// +0x7c
	void*				fMixBuffer;				// +0xb8
	void				(*fMix)(void*, void*, long);	// +0xbc
	long				fSilence;				// +0xc0
	TSoundChannel*		fOutputChannels;		// +0xc4
	void*				fDMABuffer[2];			// +0xc8
	long				fDMASize[2];			// +0xd0
	long				fDMAIndex;				// +0xd8  the buffer filled next
	TSoundChannel*		fInputChannels;			// +0xdc
	void*				fInputBuffer[2];		// +0xe0
	long				fInputSize[2];			// +0xe8
	TSoundChannel*		fDecompressorChannels;	// +0xf8
	TSoundChannel*		fCompressorChannels;	// +0xfc
	long				fVolume;				// +0x100
};

extern long		gMaxFilterNodes;		// ROM 0x0c101b18 gMaxFilterNodes
extern long		gNumFilterNodes;		// ROM 0x0c101b1c gNumFilterNodes

void	MixLin16(void* dst, void* src, long count);		// ROM 0x001e7380 MixLin16__FPvT1l

#endif	/* __SOUNDSERVER_H */

/*
	File:		protocols/Protocols.h

	Contains:	Protocols, protocol meta-information (TClassInfo) and the
				protocol registry - the DDK's OS600/Protocols.h re-expressed
				for a portable build.  The DDK's header is not used (it is
				excluded by tools/newton-rom/sync_ddk_headers.py); this one
				keeps its names and public interface.

	A "protocol" is Newton's interface/implementation mechanism: an
	interface (PROTOCOL TFoo : public TProtocol) declares methods, an
	implementation (TFooImpl : public TFoo) supplies them and a TClassInfo
	describing itself - names, version, capability signature, size, how to
	make and destroy an instance - which it registers with the protocol
	registry; clients make instances by name (NewByName("TFoo", "TFooImpl"))
	and call through the interface.  A MONITOR protocol is one whose
	instance runs as a monitor task: its interface's methods send the call
	to the monitor with a selector.

	How the ROM does it, and how this build does it:

	* Dispatch.  The ROM's interface methods are ProtocolGen glue that jumps
	  through the instance's dispatch table (fBTable: a `B` per method -
	  slot 0 unused, 1 ClassInfo, 2 New, 3 Delete, then the methods in
	  declaration order); a TClassInfo is a relocatable table of
	  self-relative offsets and `B` instructions (headers/OS600/Protocols.h;
	  tools/newton-rom/analysis/classinfo.py decodes them).  Here the
	  interface's methods are C++ virtual functions the implementation
	  overrides (the DDK's own VIRTUAL/ENDVIRTUAL, the "hasNoProtocols" way
	  of compiling protocols as classes), so a TProtocol-derived object
	  carries a vtable pointer before the TProtocol fields, and a TClassInfo
	  holds function pointers where the ROM has branches.  The New(char*)
	  and Delete() every interface has are still glue, written by hand:
	  AllocInstanceByName + the implementation's New(), and the
	  implementation's Delete() + FreeInstance (see TProtocol::GlueNew,
	  GlueDelete; for a monitor, the monitor call + DestroyMonitor).

	* Monitors.  The ROM's monitor entry (generated per implementation)
	  takes a selector and a block of argument words, the interface glue
	  packs the arguments and calls MonitorDispatchSWI; here the entry is
	  an ordinary function switching on the selector (selector n is
	  dispatch slot n + 2: 0 New, 1 Delete, 2 the first method), and the
	  arguments travel in a ProtocolMonitorArgs.

	* Making an instance.  TClassInfo::MakeAt in the ROM only fills in the
	  TProtocol fields - an implementation has no constructor; its New()
	  initialises it.  Here MakeAt must also set the vtable pointer, so a
	  class info carries fMakeAt (a placement new of the implementation,
	  whose default constructor does nothing else) where the ROM keeps the
	  dispatch table's offset.  And since the vtable pointer comes first,
	  the TProtocol part of an instance is not at the start of its memory:
	  fMakeAt answers the TProtocol* (every pointer to an instance is one,
	  and a cast to the interface type adjusts it), and fRuntime, which the
	  ROM leaves 0, keeps the address the instance was made at - what
	  FreeInstance, Destroy and DestroyMonitor give back.

	* TProtocol::Become(const TProtocol*) (forward every call to another
	  instance) has no callers in the ROM and cannot be expressed with
	  virtual dispatch; SetType only changes what ClassInfo() answers.

	The TClassInfo an implementation needs is made by PROTOCOL_CLASSINFO in
	its .cpp (what ProtocolGen generated), after the DDK's
	PROTOCOL_IMPL_HEADER_MACRO in its declaration.
*/

#ifndef __PROTOCOLS_H
#define __PROTOCOLS_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif

#include <new>
#include <stddef.h>


// ProtocolGen's keywords, as the DDK defines them when not generating
#define PROTOCOL			class
#define MONITOR				class
#define	PSEUDOSTATIC		/*nothing*/
#define	NONVIRTUAL			/*nothing*/
#define	INVISIBLE			/*nothing*/
#define PROTOCOLVERSION(x)	/*nothing*/
#define MONITORVERSION(x)	/*nothing*/
#define CAPABILITIES(x)		/*nothing*/

// protocols are compiled as classes: VIRTUAL functions are pure virtual
#define VIRTUAL				virtual
#define ENDVIRTUAL			= 0

#define	PROTOCOLGEN_WORKING_DELETES				/* Delete() methods work now */

class TProtocol;
class TClassInfo;
MONITOR TClassInfoRegistry;


/*	----------------------------------------------------------------
**
**	CodeProcPtr  --  Pass control to the first byte of a hunk-O-code
**						with a selector as the first argument.
*/
typedef void* (*CodeProcPtr)(int selector, ...);

// A monitor protocol's entry: the instance the monitor was started on, the
// selector, and the call's arguments (a ProtocolMonitorArgs); 0 when the
// selector was handled, -1 otherwise.
typedef long (*ProtocolEntryProc)(void* instance, ULong selector, void* args);

// What a monitor call carries (the ROM: the result slot and the caller's
// argument registers, pushed by the glue).
struct ProtocolMonitorArgs
{
	Long			fResult;		// a word: an error, a count or a pointer
	ULong			fArg[4];
};


/*	----------------------------------------------------------------
**
**	TProtocol  --  Base class for protocols and protocol monitors
**
*/
class TProtocol
{
public:
	void			Become(const TProtocol*);			// forward to an instance
	void			Become(TObjectId);					// forward to a monitor (via kernel id)
	const TClassInfo*	ClassInfo() const;				// ==> info about the protocol (doesn't work for monitors)
	TObjectId		GetMonitorId() const;				// ==> monitor id, or zero
	operator		TObjectId();						// ==> monitor id, or zero
	void			SetType(const TClassInfo*);			// set this instance's type
	long			StartMonitor						// start object up as a monitor
						(
							unsigned long	stackSize,
							TObjectId		environment = 0,
							ULong			name = 0x6D6E7472,		// Hardcode 'mntr' for Win compilers
							Boolean			rebootProtected = false
						);
	long			DestroyMonitor();					// destroy the monitor

	// what the generated glue of every interface's New(char*) and Delete()
	// does once the instance exists (a protocol's), or the monitor call
	// the methods of a MONITOR interface make
	TProtocol*		GlueNew();
	void			GlueDelete();
	Long			MonitorCall(ULong selector, ProtocolMonitorArgs* args);

	/*
	**	Never change these
	**	 (generated glue depends on this layout)
	**
	*/
	void*			fRuntime;				// +0x00  runtime usage (e.g. Throw() cleanup of autos for exceptions); here the instance's memory
	const TProtocol*	fRealThis;			// +0x04  -> true "this" for auto instances
	const TClassInfo*	fBTable;			// +0x08  -> dispatch table (the ROM); the class info here, dispatch being by vtable
	TObjectId		fMonitorId;				// +0x0c  for monitors, the monitor id
};

inline TProtocol :: operator TObjectId()
{
	return fMonitorId;
}


/*
**	Put one of these inside the '{...}' of your protocol implementations.
**	Follow it with a semicolon.
*/
#define	PROTOCOL_IMPL_HEADER_MACRO(name) \
	static size_t Sizeof(); \
	static const TClassInfo* ClassInfo(void)

/*
**	For each of your protocol implementations, put one of these in your
**	".c" or ".cp" files.
*/
#define	PROTOCOL_IMPL_SOURCE_MACRO(name) \
	size_t name :: Sizeof() { return sizeof(name); }

/*
**	And one of these: the TClassInfo ProtocolGen generates for the
**	implementation.  interface, signature: strings; version, flags: numbers;
**	entry: the monitor entry (a ProtocolEntryProc), or nil.  The
**	implementation's New() returns its own type and Delete() returns void.
*/
#define PROTOCOL_CLASSINFO(name, interface, signature, version, flags, entry) \
	static TProtocol* name##_MakeAt(void* at) { return new (at) name; } \
	static TProtocol* name##_New(TProtocol* p) { return (TProtocol*) ((name*) p)->New(); } \
	static void name##_Delete(TProtocol* p) { ((name*) p)->Delete(); } \
	const TClassInfo* name :: ClassInfo() \
	{ \
		static const TClassInfo info = { 0, #name, interface, signature, name##_MakeAt, entry, \
			name::Sizeof, nil, nil, name##_New, name##_Delete, version, flags, nil, 0 }; \
		return &info; \
	}


/*	----------------------------------------------------------------
**
**	TClassInfo  --  Meta-information for protocols
**
*/
class TClassInfo
{
public:
	const char *	ImplementationName()	const;	// implementation name
	const char *	InterfaceName()			const;	// name of public interface
	const char *	Signature()				const;	// signature (actually, capability list)
	size_t			Size()					const;	// instance size
	void			MakeAt(const void*)		const;	// construct an instance at the address
	const void *	EntryProc()				const;	// return address of monitor entry proc
	const void *	AllocProc()				const;	// return address of OperatorNew() proc, or nil
	const void *	FreeProc()				const;	// return address of OperatorDelete() proc, or nil
	unsigned long	Version()				const;	// implementation version
	unsigned long	Flags()					const;	// various flags (see below)
	long			Register()				const;	// register with protocol-server
	long			DeRegister()			const;	// de-register with protocol-server
	TProtocol *		New()					const;	// make an instance
	void			Destroy(TProtocol *)	const;	// destroy an instance at the address
	CodeProcPtr		Selector()				const;	// return address of selector proc
	const char *	GetCapability(const char*)	const;	// test if protocol has a specific capability, return it
	const char *	GetCapability(long)	const;		// test if protocol has a specific capability, return it
	Boolean			HasInstances(long *count) const; 	// return true is instances of this protocol exist, count = number of them

	/*
	**	The ROM's table, word for word (headers/OS600/Protocols.h): a
	**	self-relative offset (SRO) becomes a pointer, an ARM branch a
	**	function pointer.  Public so that PROTOCOL_CLASSINFO can build one.
	**
	*/
	long			fReserved1;				// (reserved for future use, zero for now)
	const char*		fName;					// SRO to asciz implementation name
	const char*		fInterface;				// SRO to asciz protocol name
	const char*		fSignature;				// SRO to asciz signature
	TProtocol*		(*fMakeAt)(void*);		// SRO to dispatch table; here the construction that sets the vtable pointer
	ProtocolEntryProc	fEntryProc;			// SRO to monitor entry (valid only for monitors)
	size_t			(*fSizeof)();			// ARM branch to sizeof-code
	void*			(*fAlloc)();			// ARM branch to alloc-code, or zero (the instance's memory)
	void			(*fFree)(void*);		// ARM branch to OperatorDelete code, or zero
	TProtocol*		(*fDefaultNew)(TProtocol*);		// ARM branch to New(void), or MOV PC,LK
	void			(*fDefaultDelete)(TProtocol*);	// ARM branch to Delete(void), or MOV PC,LK
	unsigned long	fVersion;				// this implementation's version
	unsigned long	fFlags;					// various flags (see below)
	CodeProcPtr		fSelector;				// ARM branch to bail-out function (that returns nil now)
	long			fReserved2;				// (reserved for future use, zero for now)
};

enum
{
	 kci_IsMonitor		= 0x00000001		// bit 0 ==> is monitor
};


/*	----------------------------------------------------------------
**
**	TClassInfoRegistry  --  A registry of protocols
**
*/
MONITOR TClassInfoRegistry : public TProtocol
{
public:
	static TClassInfoRegistry* New(const char*);
	void			Delete();
	NewtonErr		Register(const TClassInfo*, ULong refCon=0);
	NewtonErr		DeRegister(const TClassInfo*, Boolean specific=false);
	Boolean			IsRegistered(const TClassInfo*, Boolean specific=false) const;
	const TClassInfo* Satisfy(const char* intf, const char* impl, ULong version) const;
	long			Seed() const;
	const TClassInfo*	First(long seed, ULong* pRefCon=0) const;
	const TClassInfo*	Next(long seed, const TClassInfo* from, ULong* pRefCon=0) const;
	const TClassInfo*	Find(const char* intf, const char* impl, int skipCount, ULong* pRefCon=0) const;
	//	2.0 calls
	const TClassInfo*	Satisfy(const char* intf, const char* impl, const char* capability) const;
	const TClassInfo*	Satisfy(const char* intf, const char* impl, const char* capability, const char* capabilityValue) const;
	const TClassInfo*	Satisfy(const char* intf, const char* impl, const long capability, const long capabilityValue = 0) const;
	void				UpdateInstanceCount(const TClassInfo* classinfo, long adjustment);
	long 				GetInstanceCount(const TClassInfo* classinfo);
};

// the monitor selectors of TClassInfoRegistry (dispatch slot - 2)
enum
{
	kClassInfoRegistry_New,
	kClassInfoRegistry_Delete,
	kClassInfoRegistry_Register,
	kClassInfoRegistry_DeRegister,
	kClassInfoRegistry_IsRegistered,
	kClassInfoRegistry_Satisfy,
	kClassInfoRegistry_Seed,
	kClassInfoRegistry_First,
	kClassInfoRegistry_Next,
	kClassInfoRegistry_Find,
	kClassInfoRegistry_SatisfyCapability,
	kClassInfoRegistry_SatisfyCapabilityValue,
	kClassInfoRegistry_SatisfyLongCapability,
	kClassInfoRegistry_UpdateInstanceCount,
	kClassInfoRegistry_GetInstanceCount
};

extern TClassInfoRegistry*		gProtocolRegistry;
extern TProtocol*				NewByName(const char * abstract, const char * implementation);
extern TProtocol*				NewByName(const char * abstract, const char * implementation, ULong version);	// ask for version
extern TProtocol*				NewByName(const char * abstract, const char * implementation, const char * capability);
extern const TClassInfo*		ClassInfoByName(const char * abstract, const char * implementation, ULong version = 0);
extern TProtocol*				AllocInstanceByName(const char * abstract, const char * implementation);
extern void						FreeInstance(TProtocol*);
extern void						StartupProtocolRegistry(void);
extern TClassInfoRegistry*		GetProtocolRegistry(void);

// hunks of code (a code block entered with a selector)
extern const TClassInfo*		ClassInfoFromHunkByName(void* hunk, const char* abstract, const char* implementation);
extern TProtocol*				NewFromHunkByName(void* hunk, const char* abstract, const char* implementation);

// the pre-jump-table forms the boot uses
extern size_t					PrivateClassInfoSize(const TClassInfo*);
extern TProtocol*				PrivateClassInfoMakeAt(const TClassInfo*, const void* proto);	// (void in the DDK; the instance here)
extern const char *				PrivateClassInfoInterfaceName(const TClassInfo*);
extern const char *				PrivateClassInfoImplementationName(const TClassInfo*);


/*	----------------------------------------------------------------
**
**	Standard selectors for hunks-of-code
**
*/
enum {
	 kCodeInit			= 0			// int (*code)(kCodeInit);	// initialization, returns zero on success
	,kCodeVersion		= 1			// int (*code)(kCode);		// version, zero for now
	,kCodeInfo			= 2			// int (*code)(kCode);		// any number you like
	,kCodeClassCount	= 3			// int (*code)(kCode);		// ==> # classes
	,kCodeClassAt		= 4			// const TClassInfo* (*code)(kCode, int Nth);	// class-info-proc for Nth class
	,kCodeReserved5		= 5			// reserved
	,kCodeReserved6		= 6			// reserved
	,kCodeReserved7		= 7			// reserved
	// First generally available selector is 8
};

/*	----------------------------------------------------------------
**
**	SAFELY_CAST  --  safely cast an instance of a protocol to some
**					 expected implementation
**
**		o	result is zero if the implementation isn't the exact
**			expected one
**
*/
#define	SAFELY_CAST(instance, to) 	instance->ClassInfo() == to::ClassInfo() ? (to*)instance : 0

#endif	/* __PROTOCOLS_H */

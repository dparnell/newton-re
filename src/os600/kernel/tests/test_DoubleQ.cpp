// Host unit test for TDoubleQContainer / TDoubleQItem (src/os600/kernel/DoubleQ.*).
// Exercises the reconstructed behaviour: FIFO append, AddToFront, AddBefore,
// removal from head/middle/tail, membership checks and the destructor hook.

#include "DoubleQ.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

// A queued object with the TDoubleQItem somewhere inside it, as kernel objects have.
struct Thing
{
	int				id;
	int				padding;
	TDoubleQItem	qItem;
	int				more;
};

static Thing* Make(int id)
{
	Thing* t = new Thing;
	t->id = id;
	t->more = 0;
	return t;
}

static int destroyed[8];
static int numDestroyed = 0;
static void* destroyedInstance = nil;

static void Destructor(void* instance, char* item)
{
	destroyedInstance = instance;
	destroyed[numDestroyed++] = ((Thing*) item)->id;
}

int main()
{
	const ULong offset = offsetof(Thing, qItem);
	Thing* a = Make(1);
	Thing* b = Make(2);
	Thing* c = Make(3);
	Thing* d = Make(4);

	// empty queue
	TDoubleQContainer q(offset);
	EXPECT(q.Peek() == nil);
	EXPECT(q.Remove() == nil);
	EXPECT(q.GetNext(a) == nil);
	EXPECT(!q.RemoveFromQueue(a));
	EXPECT(!q.RemoveFromQueue(nil));

	// Add is FIFO
	q.Add(a);
	q.Add(b);
	q.Add(c);
	EXPECT(q.Peek() == a);
	EXPECT(q.GetNext(a) == b);
	EXPECT(q.GetNext(b) == c);
	EXPECT(q.GetNext(c) == nil);
	EXPECT(a->qItem.fContainer == &q);

	// AddToFront / AddBefore
	q.AddToFront(d);						// d a b c
	EXPECT(q.Peek() == d);
	EXPECT(q.GetNext(d) == a);
	EXPECT(q.RemoveFromQueue(d));			// a b c
	EXPECT(d->qItem.fContainer == nil);
	q.AddBefore(b, d);						// a d b c
	EXPECT(q.GetNext(a) == d);
	EXPECT(q.GetNext(d) == b);
	EXPECT(q.RemoveFromQueue(d));
	q.AddBefore(a, d);						// d a b c  (before the head == AddToFront)
	EXPECT(q.Peek() == d);
	EXPECT(q.RemoveFromQueue(d));			// a b c

	// remove from the middle, the tail, then the head
	EXPECT(q.RemoveFromQueue(b));			// a c
	EXPECT(q.GetNext(a) == c);
	EXPECT(!q.RemoveFromQueue(b));			// not queued any more
	EXPECT(q.RemoveFromQueue(c));			// a
	EXPECT(q.GetNext(a) == nil);
	EXPECT(q.Remove() == a);					// empty
	EXPECT(q.Peek() == nil);
	EXPECT(q.Remove() == nil);

	// an item in another container is not ours
	TDoubleQContainer other(offset);
	other.Add(b);
	EXPECT(q.GetNext(b) == nil);
	EXPECT(!q.RemoveFromQueue(b));
	EXPECT(other.Remove() == b);

	// DeleteFromQueue calls the destructor with the instance and the item
	int instance = 42;
	TDoubleQContainer dq(offset, Destructor, &instance);
	dq.Add(a);
	dq.Add(b);
	EXPECT(dq.DeleteFromQueue(b));
	EXPECT(!dq.DeleteFromQueue(c));
	EXPECT(numDestroyed == 1 && destroyed[0] == 2 && destroyedInstance == &instance);
	EXPECT(dq.Remove() == a);

	// without a destructor DeleteFromQueue just removes
	q.Add(c);
	EXPECT(q.DeleteFromQueue(c));
	EXPECT(numDestroyed == 1);
	EXPECT(q.Peek() == nil);

	delete a; delete b; delete c; delete d;
	if (failures == 0)
		printf("test_DoubleQ: all checks passed\n");
	return failures == 0 ? 0 : 1;
}

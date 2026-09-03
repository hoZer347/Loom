#include "doctest.h"

#include "DataPackage.h"

#include <cstring>
#include <string>
#include <type_traits>

// DataPackage is the only part of Loom Networking these tests reach. The socket
// types are deliberately left out: TCPServer binds its acceptor and TCPClient
// opens its io_context in member initialisers, so merely constructing one takes
// a real port, and their headers pull in Boost, which is not vendored with the
// repository. Neither is true of DataPackage, which is header-only and builds
// for the web as readily as for the desktop.

namespace
{
	// A stand-in for a real wire message. DataPackage is CRTP-style: the derived
	// type is handed back to the base so Deserialize knows how many bytes the
	// whole package occupies.
	struct Ping final : Loom::DataPackage<Ping, 7>
	{
		void Handle() override { handled = true; };

		bool handled = false;
		int  sequence = 0;
		char payload[8] = { };
	};

	struct Pong final : Loom::DataPackage<Pong, 9>
	{
		void Handle() override { };
	};
};


TEST_SUITE("DataPackage")
{
	TEST_CASE("the template ID becomes a per-type instance field")
	{
		CHECK(Ping{ }.ID == 7);
		CHECK(Pong{ }.ID == 9);
	};

	TEST_CASE("the ID is the wire tag, so it is fixed per package type")
	{
		Ping first, second;

		CHECK(first.ID == second.ID);
		CHECK(first.ID != Pong{ }.ID);
	};

	TEST_CASE("Handle dispatches through the base interface")
	{
		Ping ping;
		Loom::DataPackage<Ping, 7>& asBase = ping;

		REQUIRE_FALSE(ping.handled);

		asBase.Handle();

		CHECK(ping.handled);
	};

	TEST_CASE("Handle is the one thing every package must implement")
	{
		CHECK(std::is_abstract_v<Loom::DataPackage<Ping, 7>>);
		CHECK_FALSE(std::is_abstract_v<Ping>);
	};

	// The default Serialize is a zero-copy view: it hands back the object's own
	// address rather than allocating a buffer, so the caller must not free it or
	// outlive the package.
	TEST_CASE("default Serialize aliases the package itself")
	{
		Ping ping;

		CHECK(ping.Serialize() == static_cast<void*>(&ping));
	};

	TEST_CASE("Deserialize round-trips a package of the same type")
	{
		Ping source;
		source.sequence = 42;
		std::memcpy(source.payload, "loom", sizeof("loom"));

		Ping destination;
		REQUIRE(destination.sequence == 0);

		destination.Deserialize(source.Serialize());

		CHECK(destination.sequence == 42);
		CHECK(std::string(destination.payload) == "loom");
		CHECK(destination.ID == 7);
	};

	TEST_CASE("Deserialize copies the whole derived package, not just the base")
	{
		Ping source;
		source.handled  = true;
		source.sequence = -1;
		std::memcpy(source.payload, "tail", sizeof("tail"));

		Ping destination;
		destination.Deserialize(source.Serialize());

		// payload is the last member, so seeing it arrive proves the copy is
		// sized by the derived type rather than by DataPackage itself.
		CHECK(destination.handled);
		CHECK(destination.sequence == -1);
		CHECK(std::string(destination.payload) == "tail");
	};

	// Deserialize memcpys sizeof(BASE) bytes over *this, which includes the
	// vtable pointer and the const ID field. It only works because source and
	// destination are the same concrete type and so share a vtable; feeding it
	// bytes from a different package type would leave a corrupt object. Recorded
	// here so the constraint is visible at the call site.
	TEST_CASE("a round-tripped package still dispatches virtually")
	{
		Ping source;
		Ping destination;

		destination.Deserialize(source.Serialize());

		Loom::DataPackage<Ping, 7>& asBase = destination;
		asBase.Handle();

		CHECK(destination.handled);
	};

	TEST_CASE("a package can be overwritten more than once")
	{
		Ping first;
		first.sequence = 1;

		Ping second;
		second.sequence = 2;

		Ping destination;

		destination.Deserialize(first.Serialize());
		CHECK(destination.sequence == 1);

		destination.Deserialize(second.Serialize());
		CHECK(destination.sequence == 2);
	};
};

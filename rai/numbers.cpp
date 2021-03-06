#include <rai/numbers.hpp>

#include <ed25519-donna/ed25519.h>

#include <blake2/blake2.h>

#include <cryptopp/aes.h>
#include <cryptopp/modes.h>

thread_local CryptoPP::AutoSeededRandomPool rai::random_pool;

namespace
{
	char const * base58_reverse ("~012345678~~~~~~~9:;<=>?@~ABCDE~FGHIJKLMNOP~~~~~~QRSTUVWXYZ[~\\]^_`abcdefghi");
	uint8_t base58_decode (char value)
	{
		assert (value >= '0');
		assert (value <= '~');
		auto result (base58_reverse [value - 0x30] - 0x30);
		return result;
	}
	char const * account_lookup ("13456789abcdefghijkmnopqrstuwxyz");
	char const * account_reverse ("~0~1234567~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~89:;<=>?@AB~CDEFGHIJK~LMNO~~~~~");
	char account_encode (uint8_t value)
	{
		assert (value < 32);
		auto result (account_lookup [value]);
		return result;
	}
	uint8_t account_decode (char value)
	{
		assert (value >= '0');
		assert (value <= '~');
		auto result (account_reverse [value - 0x30] - 0x30);
		return result;
	}
}

void rai::uint256_union::encode_account (std::string & destination_a) const
{
	assert (destination_a.empty ());
	destination_a.reserve (64);
	uint64_t check (0);
	blake2b_state hash;
	blake2b_init (&hash, 5);
	blake2b_update (&hash, bytes.data (), bytes.size ());
	blake2b_final (&hash, reinterpret_cast <uint8_t *> (&check), 5);
	rai::uint512_t number_l (number ());
	number_l <<= 40;
	number_l |= rai::uint512_t (check);
	for (auto i (0); i < 60; ++i)
	{
		auto r (number_l.convert_to <uint8_t> () & 0x1f);
		number_l >>= 5;
		destination_a.push_back (account_encode (r));
	}
	destination_a.append ("_brx"); // xrb_
	std::reverse (destination_a.begin (), destination_a.end ());
}

std::string rai::uint256_union::to_account_split () const
{
	auto result (to_account ());
	assert (result.size () == 64);
	result.insert (32, "\n");
	return result;
}

std::string rai::uint256_union::to_account () const
{
	std::string result;
	encode_account (result);
	return result;
}

bool rai::uint256_union::decode_account_v1 (std::string const & source_a)
{
	auto result (source_a.size () != 50);
	if (!result)
	{
		rai::uint512_t number_l;
		for (auto i (source_a.begin ()), j (source_a.end ()); !result && i != j; ++i)
		{
			uint8_t character (*i);
			result = character < 0x30 || character >= 0x80;
			if (!result)
			{
				uint8_t byte (base58_decode (character));
				result = byte == '~';
				if (!result)
				{
					number_l *= 58;
					number_l += byte;
				}
			}
		}
		if (!result)
		{
			*this = number_l.convert_to <rai::uint256_t> ();
			uint32_t check ((number_l >> 256).convert_to <uint32_t> ());
			result = (number_l >> (256 + 32)) != 13;
			if (!result)
			{
				uint32_t validation;
				blake2b_state hash;
				blake2b_init (&hash, sizeof (validation));
				blake2b_update (&hash, bytes.data (), sizeof (bytes));
				blake2b_final (&hash, reinterpret_cast <uint8_t *> (&validation), sizeof (validation));
				result = check != validation;
			}
		}
	}
	return result;
}

bool rai::uint256_union::decode_account (std::string const & source_a)
{
	auto result (source_a.size () != 64);
	if (!result)
	{
		if (source_a [0] == 'x' && source_a [1] == 'r' && source_a [2] == 'b' && (source_a [3] == '_' || source_a [3] == '-'))
		{
			rai::uint512_t number_l;
			for (auto i (source_a.begin () + 4), j (source_a.end ()); !result && i != j; ++i)
			{
				uint8_t character (*i);
				result = character < 0x30 || character >= 0x80;
				if (!result)
				{
					uint8_t byte (account_decode (character));
					result = byte == '~';
					if (!result)
					{
						number_l <<= 5;
						number_l += byte;
					}
				}
			}
			if (!result)
			{
				*this = (number_l >> 40).convert_to <rai::uint256_t> ();
				uint64_t check (number_l.convert_to <uint64_t> ());
				check &=  0xffffffffff;
				uint64_t validation (0);
				blake2b_state hash;
				blake2b_init (&hash, 5);
				blake2b_update (&hash, bytes.data (), bytes.size ());
				blake2b_final (&hash, reinterpret_cast <uint8_t *> (&validation), 5);
				result = check != validation;
			}
		}
		else
		{
			result = true;
		}
	}
	else
	{
		result = decode_account_v1 (source_a);
	}
	return result;
}

rai::uint256_union::uint256_union (rai::uint256_t const & number_a)
{
	rai::uint256_t number_l (number_a);
	for (auto i (bytes.rbegin ()), n (bytes.rend ()); i != n; ++i)
	{
		*i = ((number_l) & 0xff).convert_to <uint8_t> ();
		number_l >>= 8;
	}
}

bool rai::uint256_union::operator == (rai::uint256_union const & other_a) const
{
	return bytes == other_a.bytes;
}

// Construct a uint256_union = AES_ENC_CTR (cleartext, key, iv)
void rai::uint256_union::encrypt (rai::raw_key const & cleartext, rai::raw_key const & key, uint128_union const & iv)
{
	CryptoPP::AES::Encryption alg (key.data.bytes.data (), sizeof (key.data.bytes));
	CryptoPP::CTR_Mode_ExternalCipher::Encryption enc (alg, iv.bytes.data ());
	enc.ProcessData (bytes.data (), cleartext.data.bytes.data (), sizeof (cleartext.data.bytes));
}

bool rai::uint256_union::is_zero () const
{
    return qwords [0] == 0 && qwords [1] == 0 && qwords [2] == 0 && qwords [3] == 0;
}

std::string rai::uint256_union::to_string () const
{
	std::string result;
	encode_hex (result);
	return result;
}

bool rai::uint256_union::operator < (rai::uint256_union const & other_a) const
{
	return number () < other_a.number ();
}

rai::uint256_union & rai::uint256_union::operator ^= (rai::uint256_union const & other_a)
{
	auto j (other_a.qwords.begin ());
	for (auto i (qwords.begin ()), n (qwords.end ()); i != n; ++i, ++j)
	{
		*i ^= *j;
	}
	return *this;
}

rai::uint256_union rai::uint256_union::operator ^ (rai::uint256_union const & other_a) const
{
	rai::uint256_union result;
	auto k (result.qwords.begin ());
	for (auto i (qwords.begin ()), j (other_a.qwords.begin ()), n (qwords.end ()); i != n; ++i, ++j, ++k)
	{
		*k = *i ^ *j;
	}
	return result;
}

rai::uint256_union::uint256_union (std::string const & hex_a)
{
	decode_hex (hex_a);
}

void rai::uint256_union::clear ()
{
	qwords.fill (0);
}

rai::uint256_t rai::uint256_union::number () const
{
	rai::uint256_t result;
	auto shift (0);
	for (auto i (bytes.begin ()), n (bytes.end ()); i != n; ++i)
	{
		result <<= shift;
		result |= *i;
		shift = 8;
	}
	return result;
}

void rai::uint256_union::encode_hex (std::string & text) const
{
	assert (text.empty ());
	std::stringstream stream;
	stream << std::hex << std::noshowbase << std::setw (64) << std::setfill ('0');
	stream << number ();
	text = stream.str ();
}

bool rai::uint256_union::decode_hex (std::string const & text)
{
	auto result (false);
	if (!text.empty ())
	{
		if (text.size () <= 64)
		{
			std::stringstream stream (text);
			stream << std::hex << std::noshowbase;
			rai::uint256_t number_l;
			try
			{
				stream >> number_l;
				*this = number_l;
				if (!stream.eof ())
				{
					result = true;
				}
			}
			catch (std::runtime_error &)
			{
				result = true;
			}
		}
		else
		{
			result = true;
		}
	}
	else
	{
		result = true;
	}
	return result;
}

void rai::uint256_union::encode_dec (std::string & text) const
{
	assert (text.empty ());
	std::stringstream stream;
	stream << std::dec << std::noshowbase;
	stream << number ();
	text = stream.str ();
}

bool rai::uint256_union::decode_dec (std::string const & text)
{
	auto result (text.size () > 78);
	if (!result)
	{
		std::stringstream stream (text);
		stream << std::dec << std::noshowbase;
		rai::uint256_t number_l;
		try
		{
			stream >> number_l;
			*this = number_l;
		}
		catch (std::runtime_error &)
		{
			result = true;
		}
	}
	return result;
}

rai::uint256_union::uint256_union (uint64_t value0)
{
	*this = rai::uint256_t (value0);
}

bool rai::uint256_union::operator != (rai::uint256_union const & other_a) const
{
	return ! (*this == other_a);
}

bool rai::uint512_union::operator == (rai::uint512_union const & other_a) const
{
	return bytes == other_a.bytes;
}

rai::uint512_union::uint512_union (rai::uint512_t const & number_a)
{
	rai::uint512_t number_l (number_a);
	for (auto i (bytes.rbegin ()), n (bytes.rend ()); i != n; ++i)
	{
		*i = ((number_l) & 0xff).convert_to <uint8_t> ();
		number_l >>= 8;
	}
}

void rai::uint512_union::clear ()
{
	bytes.fill (0);
}

rai::uint512_t rai::uint512_union::number () const
{
	rai::uint512_t result;
	auto shift (0);
	for (auto i (bytes.begin ()), n (bytes.end ()); i != n; ++i)
	{
		result <<= shift;
		result |= *i;
		shift = 8;
	}
	return result;
}

void rai::uint512_union::encode_hex (std::string & text) const
{
	assert (text.empty ());
	std::stringstream stream;
	stream << std::hex << std::noshowbase << std::setw (128) << std::setfill ('0');
	stream << number ();
	text = stream.str ();
}

bool rai::uint512_union::decode_hex (std::string const & text)
{
	auto result (text.size () > 128);
	if (!result)
	{
		std::stringstream stream (text);
		stream << std::hex << std::noshowbase;
		rai::uint512_t number_l;
		try
		{
			stream >> number_l;
			*this = number_l;
			if (!stream.eof ())
			{
				result = true;
			}
		}
		catch (std::runtime_error &)
		{
			result = true;
		}
	}
	return result;
}

bool rai::uint512_union::operator != (rai::uint512_union const & other_a) const
{
	return ! (*this == other_a);
}

rai::uint512_union & rai::uint512_union::operator ^= (rai::uint512_union const & other_a)
{
	uint256s [0] ^= other_a.uint256s [0];
	uint256s [1] ^= other_a.uint256s [1];
	return *this;
}

rai::raw_key::~raw_key ()
{
	data.clear ();
}

bool rai::raw_key::operator == (rai::raw_key const & other_a) const
{
	return data == other_a.data;
}

bool rai::raw_key::operator != (rai::raw_key const & other_a) const
{
	return !(*this == other_a);
}

// This this = AES_DEC_CTR (ciphertext, key, iv)
void rai::raw_key::decrypt (rai::uint256_union const & ciphertext, rai::raw_key const & key_a, uint128_union const & iv)
{
	CryptoPP::AES::Encryption alg (key_a.data.bytes.data (), sizeof (key_a.data.bytes));
	CryptoPP::CTR_Mode_ExternalCipher::Decryption dec (alg, iv.bytes.data ());
	dec.ProcessData (data.bytes.data (), ciphertext.bytes.data (), sizeof (ciphertext.bytes));
}


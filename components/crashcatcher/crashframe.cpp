#include "crashframe.hpp"

namespace CrashCatcher
{
    namespace
    {
        // Appends to a fixed buffer and keeps the last character for the null
        struct Writer
        {
            std::span<char> mOut;
            std::size_t mUsed = 0;

            void put(char c)
            {
                if (mUsed + 1 < mOut.size())
                    mOut[mUsed++] = c;
            }

            void text(const char* value)
            {
                for (; *value != '\0'; ++value)
                    put(*value);
            }

            void decimal(unsigned int value)
            {
                char digits[16];
                std::size_t count = 0;
                do
                {
                    digits[count++] = static_cast<char>('0' + value % 10);
                    value /= 10;
                } while (value != 0);
                while (count > 0)
                    put(digits[--count]);
            }

            void hex(std::uintptr_t value)
            {
                text("0x");
                char digits[2 * sizeof(value)];
                std::size_t count = 0;
                do
                {
                    digits[count++] = "0123456789abcdef"[value & 0xf];
                    value >>= 4;
                } while (value != 0);
                while (count > 0)
                    put(digits[--count]);
            }

            std::size_t finish()
            {
                if (!mOut.empty())
                    mOut[mUsed] = '\0';
                return mUsed;
            }
        };

        const char* fileName(const char* path)
        {
            const char* name = path;
            for (const char* c = path; *c != '\0'; ++c)
                if (*c == '/')
                    name = c + 1;
            return name;
        }
    }

    std::size_t formatFrame(std::span<char> out, const char* label, std::uintptr_t address, const char* image,
        std::uintptr_t imageBase, const char* symbol, std::uintptr_t symbolAddress)
    {
        Writer writer{ out };
        writer.text(label);
        writer.put(' ');
        writer.hex(address);
        writer.put(' ');
        if (image == nullptr)
            writer.text("(no image)");
        else
        {
            writer.text(fileName(image));
            writer.put('+');
            writer.hex(address - imageBase);
            if (symbol != nullptr)
            {
                writer.put(' ');
                writer.text(symbol);
                writer.put('+');
                writer.hex(address - symbolAddress);
            }
        }
        writer.put('\n');
        return writer.finish();
    }

    std::size_t formatFrameNumber(std::span<char> out, unsigned int number)
    {
        Writer writer{ out };
        writer.put('#');
        writer.decimal(number);
        return writer.finish();
    }
}

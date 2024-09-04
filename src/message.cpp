#include "main.hpp"
#include "message.hpp"
#include "Sequence.h"

int messageCounter = 0;

ebus::Sequence m_seq_read;
ebus::Sequence m_seq_write;

const std::vector<uint8_t> push_read(const uint8_t byte)
{
    std::vector<uint8_t> vector;

    if (byte == ebus::seq_syn)
    {
        if (m_seq_read.size() > 0)
        {
            vector = m_seq_read.get_sequence();
            m_seq_read.clear();
        }
    }
    else
    {
        m_seq_read.push_back(byte);
    }

    return vector;
}

const std::vector<uint8_t> push_write(const uint8_t byte)
{
    std::vector<uint8_t> vector;

    m_seq_write.push_back(byte);

    if (m_seq_write.size() > 0)
        vector = m_seq_write.get_sequence();

    return vector;
}

void clear_write()
{
    m_seq_write.clear();
}

const std::string to_string_seq_write()
{
    return m_seq_write.to_string();
}

int pushMsgClient(WiFiClient *client, uint8_t byte)
{
    if (client->availableForWrite() >= AVAILABLE_THRESHOLD)
    {
        const std::vector<uint8_t> sequence = push_read(byte);
        client->write(sequence.data(), sequence.size());

        return 1;
    }
    return 0;
}

void handleMsgClient(WiFiClient *client)
{
    while (client->available())
    {
        messageCounter++;

        uint8_t msgBuffer[16];
        memset(&msgBuffer[0], 0, sizeof(msgBuffer));

        size_t msgSize = client->read(&msgBuffer[0], sizeof(msgBuffer));

        if (msgSize > 0)
        {
            clear_write();

            for (size_t i = 0; i < msgSize; i++)
                push_write(msgBuffer[i]);

            client->write(printMessage());
            client->stop();
        }
    }
}

char *printMessage()
{
    static char message[34];
    sprintf(message, "%s", to_string_seq_write().c_str());
    return message;
}

int printMessageCounter()
{
    return messageCounter;
}

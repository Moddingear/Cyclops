#pragma once

//Generic class to send data to other programs
class GenericTransport
{
private:
	/* data */
public:

	static void printBuffer(const void *buffer, int length);
	
	virtual void Broadcast(const void *buffer, int length);

	virtual int Receive(void *buffer, int maxlength, bool blocking=false);
};

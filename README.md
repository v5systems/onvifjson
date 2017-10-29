# onvifjson
Onvif - json

# Project:

onvifjson is a simple onvif / json proxy server.

onvifjson expects binary header

#define ONVIF_PROT_MARKER       0x5432fbca

struct Header{

  uint32_t marker;
  
  uint32_t mesID;
  
  uint32_t dataLen;
  
};

followed by request formatted as JSON.

Request:
{"command":"\<Command\>"[, "parameters":{\<Optional parameters\>}]}

and sends response formatted in the same way
  
Response:
{"status":"\<Status\>"[, "parameters":{\<Optional parameters\>}][, "reason":"\<Optionalreason\>"]}

The project is based on gSOAP toolkit:

https://sourceforge.net/projects/gsoap2/files/

http://www.cs.fsu.edu/~engelen/soap.html

https://www.genivia.com/products.html

# Building:

Build was tested on Ubuntu. Just run "./build.sh" inside the project folder.

# Usage:

./onvifjson
	-l \<login\> 
	-i \<ip or host\> 
	-v \<output verbosity\>
	-L \<listen ip\> 
	-P \<listen port\> 
	-m \<max idle time in connection (seconds) use 0 to disable\>
	-p \<password\> 


test utility is built with the proxy.

It can accept JSON commands, send them to onvifjson and return JSON responses.

./test \<hostname\> \<port\> \<command\>


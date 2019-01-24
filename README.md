# Wish

Wish – A peer-to-peer identity based application development stack. Built to enable applications to communicate securely without unncessary third-parties.

While Wish is inspired by social networking it is also applicable in areas not commonly associated with social media, such as providing an identity layer for physical devices, providing trust management tools for companies, or publishing scientific research. Wish provides a generic social network stack for building any application utilizing these features.

Wish key features and APIs

* Create/manage identities
* Manage trust-relationships between identities
* Sign/verify signatures by identities
* Create peers (register protocol handler)
* Discover available peers
* Send and receive data to/from other peers
* Manage and provide access control
* Manage connectivity

## Build

### Prerequisites

* A modern Unix-like operating system, tested on Linux and MacOSX.
* C compiler, gcc or clang. On MacOSX, the clang compiler is installed as a part Xcode.
* cmake, which is available via your operating system's package repository.

```sh
git submodule init
git submodule update
mkdir build
cd build 
cmake ..
make
```
## Install
This binary can be executed wherever, make sure the executable permissions are set ``chmod u+x``. 

Wish-core creates its config files and keystore under the user's home directory: ~/.wish. If you want save the files in the current working directory instead, you can supply the command line argument '-d'. **Note that in this case it will also require write access to the working directory.**



## Usage
```
The wish core takes the following arguments
    -b don't broadcast own uid over local discovery
    -l don't listen to local discovery broadcasts
    -i Starts the core in claimable mode, which means the first remote wish identity who 
       accesses this core can claim the admin role
    -h for displaying help
    -d Use the current directory for configuration files, instead of $HOME/.wish

    -s start accepting incoming connections ("server" mode)
    -p <port> listen for incoming connections at this TCP port (default: 37009), used by remote wish cores for reaching the local apps
    -r connect to a relay server, for accepting incoming connections via the relay.

    -a <port> start "App TCP" interface server at port (default: 9094), used by the local wish based apps for reaching out

    Direct client connection:
    -c <ip_addr> open a direct client connection to this IP addr
    -C <port> use specified TCP destination port when connecting as direct client
    -R <alias> The remote party's alias name (in local contact DB)
```

## Applications

`wish-cli`: A node.js command line interface for accessing and managing a Wish Core. See: https://www.npmjs.com/package/wish-cli

`wish-core-api`: A native node.js addon to quickly build node.js applications using Wish. See https://www.npmjs.com/package/wish-core-api.

`mist examples`: 
https://github.com/ControlThings/mist-examples-nodejs 

## Getting started
1. Once you've launched the wish daemon for the first time, you'll need to create an identity. This can be made using the wish-cli command line tool. 
```sh
wish-cli
identity.create('My Identity')
```
2. Run an app or service which uses the daemon, for instance one of the `mist-examples` from the previous section

## License

This project is licensed under the Apache 2.0 license. See LICENSE.md for details.

## Contributing

See CONTRIBUTING.md

## Acknowledgements

Part of this work has been carried out in the scope of the project Industrial Internet Standardized Interoperability (II-SI), co-funded by Tekes (Finnish Funding Agency for Innovation) contract number 5409/31/2014.

Part of this work has been carried out in the scope of the project Mist App/Wi-Fi, co-funded by Tekes (Finnish Funding Agency for Innovation) contract number  4524/31/2015.

Part of this work has been carried out in the scope of the project bIoTope which is co-funded by the European Commission under Horizon 2020 program, contract number H2020-ICT-2015/688203.

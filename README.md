<a id="readme-top"></a>

<!-- PROJECT SHIELDS -->
[![Contributors][contributors-shield]][contributors-url]
[![Forks][forks-shield]][forks-url]
[![Stargazers][stars-shield]][stars-url]
[![Issues][issues-shield]][issues-url]
[![MIT License][license-shield]][license-url]


<!-- PROJECT LOGO -->
<br />
<div align="center">
  <h3 align="center">NyanOS</h3>
  <p align="center">
    A 64-bit hobby operating system for the x86-64 architecture, built for learning and experimentation.
    <br />
    <br />
    <a href="https://github.com/NyanPham/NyanOS64/issues/new?labels=bug&template=bug-report---.md">Report Bug</a>
    ·
    <a href="https://github.com/NyanPham/NyanOS64/issues/new?labels=enhancement&template=feature-request---.md">Request Feature</a>
  </p>
</div>


<!-- TABLE OF CONTENTS -->
<details>
  <summary>Table of Contents</summary>
  <ol>
    <li>
      <a href="#about-the-project">About The Project</a>
      <ul>
        <li><a href="#built-with">Built With</a></li>
      </ul>
    </li>
    <li>
      <a href="#getting-started">Getting Started</a>
      <ul>
        <li><a href="#prerequisites">Prerequisites</a></li>
        <li><a href="#building-and-running">Building and Running</a></li>
      </ul>
    </li>
    <li><a href="#usage">Usage</a></li>
    <li><a href="#roadmap">Roadmap</a></li>
    <li><a href="#license">License</a></li>
    <li><a href="#contact">Contact</a></li>
    <li><a href="#acknowledgments">Acknowledgments</a></li>
  </ol>
</details>



<!-- ABOUT THE PROJECT -->
## About The Project

NyanOS is my hobby OS designed from the ground up for the `x86-64` architecture. I use the Limine Bootloader to boot my kernel. The primary goal of this project is to explore and learn the concepts of OSDev.

<p align="right">(<a href="#readme-top">back to top</a>)</p>

### Built With

This project is built with:

* C
* x86-64 Assembly (NASM)
* GNU Make
* Limine Bootloader
* QEMU

<p align="right">(<a href="#readme-top">back to top</a>)</p>


<!-- GETTING STARTED -->
## Getting Started

To get a local copy up and running, follow these simple steps.

### Prerequisites

Ensure you have the following tools installed on your system.
* `make`
* `qemu` (for running the OS)
* `nasm` (for assembling assembly files)
* An `x86_64-elf` cross-compiler (GCC or Clang)

### Building and Running

1. Clone the repo
   ```sh
   git clone https://github.com/NyanPham/NyanOS64.git
   cd NyanOS64
   ```
2. Run the OS
   ```sh
   make run
   ```
   This command will compile the kernel and shell, create a bootable disk image, and launch it in QEMU.

<p align="right">(<a href="#readme-top">back to top</a>)</p>


<!-- USAGE EXAMPLES -->
## Usage

Once NyanOS has booted, you will be greeted by the `NyanOS>` prompt. You can interact with the shell using the following commands:
* `hi` - Prints a friendly greeting.
* `clear` - Clears the terminal screen.
* `reboot` - Reboots the virtual machine.

<p align="right">(<a href="#readme-top">back to top</a>)</p>


<!-- ROADMAP -->
## Roadmap

- [x] Implement a complete physical and virtual memory manager.
- [x] Set up an IDT and handle CPU exceptions and interrupts.
- [x] Build a simple shell as a user-space program.
- [ ] Expand the syscall interface.
- [ ] Implement a simple filesystem.

<p align="right">(<a href="#readme-top">back to top</a>)</p>

<!-- LICENSE -->
## License

Distributed under the MIT License. See `LICENSE` for more information.

<p align="right">(<a href="#readme-top">back to top</a>)</p>


<!-- CONTACT -->
## Contact

NyanPham - (contact details)

Project Link: https://github.com/NyanPham/NyanOS64

<p align="right">(<a href="#readme-top">back to top</a>)</p>


<!-- ACKNOWLEDGMENTS -->
## Acknowledgments

* OSDev Wiki
* Limine Bootloader
* Best-README-Template

<p align="right">(<a href="#readme-top">back to top</a>)</p>


<!-- MARKDOWN LINKS & IMAGES -->
[contributors-shield]: https://img.shields.io/github/contributors/NyanPham/NyanOS64.svg?style=for-the-badge
[contributors-url]: https://github.com/NyanPham/NyanOS64/graphs/contributors
[forks-shield]: https://img.shields.io/github/forks/NyanPham/NyanOS64.svg?style=for-the-badge
[forks-url]: https://github.com/NyanPham/NyanOS64/network/members
[stars-shield]: https://img.shields.io/github/stars/NyanPham/NyanOS64.svg?style=for-the-badge
[stars-url]: https://github.com/NyanPham/NyanOS64/stargazers
[issues-shield]: https://img.shields.io/github/issues/NyanPham/NyanOS64.svg?style=for-the-badge
[issues-url]: https://github.com/NyanPham/NyanOS64/issues
[license-shield]: https://img.shields.io/github/license/NyanPham/NyanOS64.svg?style=for-the-badge
[license-url]: https://github.com/NyanPham/NyanOS64/blob/master/LICENSE

![SVT-AV1](https://github.com/Patman86/SVT-AV1-Mod-by-Patman/assets/54327252/8cb2bf9f-7c0a-4eed-b3c5-b8a308500b50)

I would like to express my sincere gratitude to 5fish for creating and maintaining this fork, and for their ongoing work to improve SVT-AV1-PSY. Their contributions are highly valued and have significantly advanced this project.

-----------------------------------------------------------------------------------------------------------------
## 5fish/SVT-AV1

**5fish/SVT-AV1** is a fork developed for both high efficiency and high fidelity AV1 encoding, and incorporates various tweaks, improvements, and features, alongside a set of easy-to-use parameters tailored specifically for 2D content such as anime and games.  

This fork includes developments from other SVT-AV1 versions - see [Acknowledgements](#Acknowledgements) for more info.  

### Usage

We've incorporated all 5fish/SVT-AV1 features for 2D content into 4 simple parameters below.  
We recommend you to start with these 4 parameters.  

When you're trying these 4 parameters, we highly recommend you to start with these parameters on their own, and not take any parameters from other SVT-AV1 variants. Many newer features in 5fish/SVT-AV1 conflict with older features typically used in other variants.  

<details>
<summary><b><code>--preset</code> adjusts the speed of the encoder.</b></summary>

¶ For the best encode quality, we only recommend `--preset 2`.  
¶ However, if you'd like a faster encode, you can try `--preset 4`, or at most, `--preset 6`.  
¶ On the other hand, even if you have time to burn, as of right now, we don't recommend `--preset 0`. `--preset 2` has a smart system to filter candidates, while `--preset 0` tests through all the candidates. This smart filtering system often outperforms doing the full tests, and for this reason `--preset 2` often gives a better result than `--preset 0`.  

</details>
<details>
<summary><b><code>--crf</code> adjusts the quality and thus filesize of the encode.</b></summary>

¶ In general, our community calls encodes with `--crf [<= 12.00]` high fidelity encodes.  
¶ We call encodes around `--crf [16.00 ~ 20.00]` higher quality encodes.  
¶ We call encodes around `--crf [24.00 ~ 28.00]` larger mini encodes, and encodes with `--crf [32.00 ~ 40.00]` tiny mini encodes.  
¶ You can make a test encode, have a look at the quality and the filesize you get, and decide what `--crf` suits your situation.  

</details>
<details>
<summary><b><code>--lineart-psy-bias</code> and <code>--texture-psy-bias</code> tunes detail retention in 5fish/SVT-AV1.</b></summary>

¶ Generally speaking, the higher you set the `-psy-bias`es, the more aggressively the encoder will try to retain information.  
¶ If you see a detail that the encoder fails to retain very well, apart from using better `--crf`, you can try to increase `--lineart-psy-bias` or `--texture-psy-bias` to encourage the encoder to retain it.  

¶ To use the `-psy-bias`es, you're supposed to set both of them together, each to a value fitting to how aggressively you want to retain the respective detail. It's not recommended to omit one of them, or both.  

¶ For high quality and high fidelity encodes, a good starting point might be `--lineart-psy-bias 5 --texture-psy-bias 4`.  
¶ If your source has weak lineart, you might want to use `--lineart-psy-bias [6-7]`.  
¶ If your source has heavy texture or heavy noise, you might want to use `--texture-psy-bias [5-7]`.  
¶ On the other hand, if your source doesn't really have much texture at all, you might want to reduce `--texture-psy-bias` to `3` or lower to save on filesize without losing any visual quality.  
¶ Similarly, if everything in your source is essentially texture, you might want to reduce `--lineart-psy-bias` to `3` to allow the encoder to go all in on texture retention.  

¶ For mini encodes, a good starting point might be `--lineart-psy-bias 3 --texture-psy-bias 3`.  
¶ You can set them higher following the same ideas as high quality and high fidelity encodes, but pay attention that, in mini encodes, with some noisy sources, setting `--texture-psy-bias` too high could induce blocking.  

¶ You should make test encodes, see how each value performs, and then decide on the values to use for your source.  

</details>

Here is an example command for a decent mini encode, using these 4 parameters and FFmpeg for input:  
```sh
ffmpeg -i SOURCE.mkv -strict -1 -f yuv4mpegpipe -pix_fmt yuv420p10le - | SvtAv1EncApp -b OUTPUT.ivf -i - --preset 2 --crf 26.00 --lineart-psy-bias 3 --texture-psy-bias 3 --color-primaries 1 --transfer-characteristics 1 --matrix-coefficients 1
```

For advanced users, you can check [Parameters.md](Docs/Parameters.md) for all available parameters and explanations on what they do. Checking Parameters.md is highly suggested over using `--help`, as detailed explanations are only available in Parameters.md.  

### Build

For standalone encoder, you can download our optimised builds for Windows, Linux, and macOS from [GitHub Releases](../../releases).  
To build it yourself, for performance, always use clang over gcc or msvc, and then run `Build/linux/build.sh --native --static --release --enable-lto --enable-pgo` to build a decent binary. For the best performance, check how we do it in the [GitHub Action](https://github.com/Akatmks/build-svt-av1).  

HandBrake builds are also available [here](https://github.com/Uranite/HandBrake-5fish-SVT-AV1#handbrake-5fish-svt-av1) thanks to Yiss.  

### Contributions

You're welcome to report any encoding issues to us. Bugs and crashes aside, if you have a source where you think the encoder is performing poorly, we're happy to have a look at it and see if we can improve the encoding quality in any way.  

For pull requests, if your feature can be contained in a toggleable commandline parameter, we'll be able to accept it much easier. If you believe your feature will benefit a wide range of sources, we'll happily make it enabled by default.  

Please note that we only judge a feature based on visual quality and never metric performance.  

### Acknowledgements

This fork and its feature set stands on the work of various entities and individuals, and would not be possible without their contributions, whether direct or indirect.

A huge thank you goes out to [Akatsumekusa](https://github.com/Akatmks), the main driving force behind new developments and continued maintenance on this fork.

In addition, thank you to the mainline team for their work on [SVT-AV1](https://gitlab.com/AOMediaCodec/SVT-AV1), the psy-ex team for their work on [SVT-AV1-PSY](https://github.com/psy-ex/svt-av1-psy) and its successors [-HDR](https://github.com/juliobbv-p/svt-av1-hdr) and [-PSYEX](https://github.com/BlueSwordM/svt-av1-psyex), and Trix for their work on [-Essential](https://github.com/nekotrix/SVT-AV1-Essential).  
Some features and improvements incorporated in this fork are pulled or adapted from the aforementioned sources.

Lastly, thank you to the many members within the AV1 encoding communities for contributing in various aspects, whether it be in the form of new & improved code, issue reports, test results, knowledge, or ideas.

### License

*5fish/SVT-AV1 does not feature license modifications from mainline SVT-AV1.*

Up to v0.8.7, SVT-AV1 is licensed under the BSD-2-clause license and the Alliance for Open Media Patent License 1.0.  
See [LICENSE](LICENSE-BSD2.md) and [PATENTS](PATENTS.md) for details.  
Starting from v0.9, SVT-AV1 is licensed under the BSD-3-clause clear license and the Alliance for Open Media Patent License 1.0.  
See [LICENSE](LICENSE.md) and [PATENTS](PATENTS.md) for details.

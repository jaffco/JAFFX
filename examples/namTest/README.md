# namTest

This example demonstrates [NAM](https://www.neuralampmodeler.com/) support for Jaffx using [MicroNAM](https://github.com/jaffco/MicroNAM).

### Adding new models

To generate a weights header for a given NAM model, use:

 ```bash
 python path/to/MicroNAM/nam_to_header.py <model.nam> <model.h>
 ```

 The Daisy Seed is only powerful enough to run `nano` models. If your favorite NAM model happens to be of a different architecture, you can distill it to a `nano` model using something like [`nam-distillery`](https://github.com/joeljaffesd/nam-distillery).
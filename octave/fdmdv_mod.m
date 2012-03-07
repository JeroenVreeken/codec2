% fdmdv_mod.m
%
% Modulator function for FDMDV modem.
%
% Copyright David Rowe 2012
% This program is distributed under the terms of the GNU General Public License 
% Version 2
%

function tx_fdm = fdmdv_mod(rawfilename, nbits)

fdmdv; % include modem code

frames = floor(nbits/(Nc*Nb));
tx_fdm = [];
gain = 1000; % Scale up to 16 bit shorts
prev_tx_symbols = sqrt(2)*ones(Nc,1)*exp(j*pi/4);

for i=1:frames
  tx_bits = get_test_bits(Nc*Nb);
  tx_symbols = bits_to_qpsk(prev_tx_symbols, tx_bits,'qpsk');
  prev_tx_symbols = tx_symbols;
  tx_baseband = tx_filter(tx_symbols);
tx_fdm = [tx_fdm real(fdm_upconvert(tx_baseband))];
end

tx_fdm *= gain;
fout = fopen(rawfilename,"wb");
fwrite(fout, tx_fdm, "short");
fclose(fout);

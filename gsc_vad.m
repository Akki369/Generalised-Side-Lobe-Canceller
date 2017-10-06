clc;
clear all;
close all;

%Mic_count = input('enter the number of mics >>> ');
Mic_count = 2;

% [filename,path] = uigetfile('.wav','select an speech file');
% Sig = strcat(path,filename);
[sig,Fs] = audioread('speech1.wav');

% [sig,Fs] = audioread(Sig);

% [filename1,path1] = uigetfile('.wav','select an noise file');
% noise = strcat(path1,filename1);
[Noise,Fsn] = audioread('engine1.wav');
% [Noise,Fsn] = audioread(noise);

%%

sig_mic1 = [sig(:,1).' sig(:,1).' sig(:,1).' sig(:,1).' sig(:,1).' sig(:,1).'];
sig_mic2 = [sig(:,2).' sig(:,2).' sig(:,2).' sig(:,2).' sig(:,2).' sig(:,2).'];

noise_mic1 = [Noise(:,1).' Noise(:,1).' Noise(:,1).' Noise(:,1).' Noise(:,1).' Noise(:,1).'];
noise_mic2 = [Noise(:,2).' Noise(:,2).' Noise(:,2).' Noise(:,2).' Noise(:,2).' Noise(:,2).'];

Mic1 = ((sig_mic1) + noise_mic1);
Mic2 = (sig_mic2 + (noise_mic2));

SIG(:,1) = Mic1.';
SIG(:,2) = Mic2.';

audiowrite('speech_noise2.wav',SIG,Fs);
% W = fir1(1,0.001,'high');

% figure;
% freqz(W);
% 
% Mic1_filtered = filter(W,1,Mic1);
% Mic2_filtered = filter(W,1,Mic2);
% 
% Mic = (Mic1_filtered + Mic2_filtered);

Mic = Mic1 + Mic2;

%%
N = length(Mic);

Filter_length = 0.004;
% Filter_length = input('Enter the length of the filter in terms of sampling frequency >>> ');

Filter_length = Filter_length*Fs;

L = round(N/Filter_length);

Mic_seg= zeros(L,Filter_length);

for i = 0 : L-1
    for j = 0 : Filter_length-1
          Mic_seg(i+1,j+1) = Mic(1,j+1+(i*Filter_length));
    end
    U1(1,i+1) = mean(Mic_seg(i+1,:));
end

e = 10^(-16);

x = zeros(L,1);
E1 = zeros(L,1);

for i = 1 : L
    for j = 1 : Filter_length
        y(i,j) = ((Mic_seg(i,j) - U1(1,i)).^2);
        x(i,1) = x(i,1) + y(i,j);
    end
    E1(i,1) = (x(i,1)./(Filter_length-1))+e;
    E1(i,1) = 10*log10(E1(i,1));
end

Mic_eng = 0;
for i = 1 : 20
    Mic_eng = (Mic_seg(i,:)*Mic_seg(i,:).')/32 + Mic_eng; 
end

Mic_eng = Mic_eng/20;

E_max = max(E1);
E_min = ceil(min(E1));

Thresh_initial = 18; %ranges from 20-30

for i = 1 : L
    if ((E1(i,1) > (E_max - Thresh_initial)) && (E1(i,1) > E_min))
        Mic_vad_out(i) = 1;
    else  
        Mic_vad_out(i) = 0;
    end
end

x = zeros(1,N);

for i = 0 : L-1
    x(1,(i*Filter_length)+1:(i+1)*Filter_length) = Mic_vad_out(i+1);
end

figure;
plot(Mic1);
hold on;
plot(x);
title('Noisy speech signal with VAD')
xlabel('Samples')
ylabel('Amplitude in V')
legend('Noisy Speech','VAD output')
hold off;
% Mic1_filt = filter(1,1,Mic1_filtered);
% Mic2_filt = filter([0 1],1,Mic2_filtered);
 
Mic1_filt = filter(1,1,Mic1);
Mic2_filt = filter([0 1],1,Mic2);

X_blocked = Mic1_filt + Mic2_filt;

X_blocked_seg = zeros(L,Filter_length);

for i = 0 : L-1
    for j = 0 : Filter_length-1
          X_blocked_seg(i+1,j+1) = X_blocked(1,j+1+(i*Filter_length));
    end
end

Beta = 0.005;

Weights = zeros(1,Filter_length);
E = zeros(L,Filter_length);
Y = zeros(L,Filter_length);

for i = 1 : L
X = X_blocked_seg(i,:);
d = Mic_seg(i,:);

X_conv = convm(X,Filter_length);

    for k = 1 : Filter_length
        A = X_conv(k,:)*X_conv(k,:)'+0.0001;
        U = Beta/A;
        Y(i,k) = Weights *(X_conv(k,:).');
        E(i,k) = d(1,k) - Y(i,k);
        if Mic_vad_out(i) == 0
            Weights = Weights + (U * E(i,k) * conj(X_conv(k,:)));
        end
    end
end

for i = 1 : L
    isolated_noise(1,(((i-1)*Filter_length)+1):(i*Filter_length)) = Y(i,:);
    enhanced_speech(1,(((i-1)*Filter_length)+1):(i*Filter_length)) = E(i,:);
end

figure;
plot(Mic1);
title('Input Speech Signal with Noise')
xlabel('Samples');
ylabel('Amplitude in V');

figure;
plot(sig_mic1);
title('Input Speech Signal without Noise')
xlabel('Samples');
ylabel('Amplitude in V');

figure;
plot(enhanced_speech);
title('Enhanced Speech')    
xlabel('Samples');
ylabel('Amplitude in V');

SNR_in = 10*log10(sum(sig_mic1.^2)./sum(noise_mic1.^2));
SNR_out = real(10*log10(sum(sig_mic1.^2)./sum((enhanced_speech).^2)));

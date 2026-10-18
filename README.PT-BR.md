# Pico2W DualSense 5 Bridge — OLED Edition

[English](README.md) · [中文](README.CN.md) · [Deutsch](README.DE.md) · [Español](README.ES.md) · [Français](README.FR.md) · [日本語](README.JA.md) · **Português (Brasil)**

> Transforme um Raspberry Pi Pico2W em um adaptador sem fio para o controle DualSense (DS5), com uma tela de status opcional na própria placa.

> **OLED Edition** é um fork de **[awalol/DS5Dongle](https://github.com/awalol/DS5Dongle)** (upstream) que acrescenta uma tela opcional Pico-OLED-1.3 de 128×64 com 11 telas (status, emparelhamento de até 4 controles em slots, seletor de cor da barra de luz com favoritos e predefinições de efeito, teste de gatilhos, inclinação do giroscópio, touch pad, diagnóstico, CPU/clock, intensidade do sinal Bluetooth, medidores VU de áudio e um menu de configurações persistente), além de uma reinicialização por combinação de botões do DS5. O upstream é a fonte oficial do firmware da ponte; este fork o acompanha e adiciona recursos extras por cima.

> **🧪 Quer os recursos mais novos? Experimente o canal de desenvolvimento.** No app: **Configurações do app → Atualizações → Desenvolvimento** (ele pergunta antes; **Voltar ao estável** traz você de volta a qualquer momento). As versões de desenvolvimento passam pelos mesmos testes automáticos a cada mudança; as estáveis também passam por todas as verificações de lançamento em hardware real e no Windows, e por um tempo de uso no dia a dia. A atual, **0.9.5-dev.19**, traz em relação à estável 0.9.4:
>
> - **DualSense Edge:** reatribua o Botão Fn e os botões traseiros, perfis personalizados do Edge no PlayStation Accessories, e desenho e tela OLED próprios
> - **Analógicos:** curvas de resposta e testes dos analógicos
> - **Microfone do controle:** ajuste de nível, cancelamento de eco, e a luz do botão de desativar o som acompanha o mudo; o mudo e o volume do microfone do PC chegam ao controle
> - **Suspensão e ativação:** o controle desliga quando o PC entra em suspensão, e o Botão PS pode ativar o PC
> - **Jogos (Linux):** escolha qualquer versão do Proton por jogo, configuração de HDR, e jogos só com XInput encontram o controle
> - **Use a OLED pelo app:** KEY0/KEY1 e os botões do controle ao lado da tela ao vivo, além de **Compartilhar a OLED** (PNG, área de transferência ou GIF)
> - **Verificação do firmware do controle** e informações sobre a saúde do controle
> - **Headset (Linux):** estéreo quando um headset é ligado ao controle, e de volta ao alto-falante quando é retirado
> - **Parear pela placa:** a OLED diz *Press PS to connect*; segure KEY0 por 2 s para parear, com contagem regressiva; sem busca sem fim em repouso
> - **Todas as mensagens em 7 idiomas**

---

## 🛠️ Comece aqui: baixe o app

**[→ Baixe o DS5 Dongle OLED Edition Configurator](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Edition/releases/latest)** (AppImage para Linux, um instalador para Windows que não precisa de direitos de administrador e o app universal para macOS; testado no Linux, Windows 11 e macOS, onde o teste rápido do macOS o instala e o executa num Mac de verdade com Apple Silicon. Ele também foi [relatado como funcionando no macOS 27.0.1](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Edition/issues/17).)

O app é tudo de que você precisa. Cada versão inclui o firmware correspondente, então o app o instala no seu Pico, mantém tudo atualizado e configura tudo:

1. Monte sua placa (veja [Hardware](#hardware) abaixo) e abra o app.
2. Conecte o Pico **segurando o botão BOOTSEL** (um Pico novinho não precisa do botão). O app o encontra e instala o firmware com um clique; nada para copiar à mão.
3. Emparelhe seu controle: segure **PS + Criar** até a barra de luz piscar. O app mostra quando ele estiver conectado.

![O app: seu controle ao vivo, nas cores dele](https://raw.githubusercontent.com/MarcelineVPQ/DS5Dongle-OLED-Config-Web/master/docs/screenshots/live-controller.png)

Seu controle aparece desenhado nas cores dele e se move conforme você o usa; clique em qualquer parte para configurá-la. O **[guia passo a passo](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Config-Web/blob/master/docs/GUIDE.md)** (em inglês) cobre tudo, da montagem da placa a cada configuração:

- **O controle:** modos e favoritos da barra de luz, efeitos dos gatilhos adaptáveis, **resposta tátil com intensidade da vibração por lado** (resposta tátil e vibração clássica, com um teste de 3 segundos), Audio Auto Haptics, alto-falante e microfone embutido (com testes), atribuições de botões (todos os botões em uma lista, inclusive o Botão Fn e os botões traseiros do DualSense Edge, e **layouts de botões salvos** que um jogo pode trocar enquanto roda, nunca gravados na flash: firmware v0.9.4), os quatro slots de emparelhamento e **Info do controle** (o próprio firmware do controle, comparado com o mais recente da Sony, a tensão da bateria e os contadores de uso).
- **Calibração:** drift no analógico, analógicos ou gatilhos que não chegam ao máximo, giroscópio que escorrega, corrigidos no Pico (por slot de emparelhamento) para todos os jogos; nada é gravado no controle. Além disso, uma **curva de sensibilidade** por analógico e testes dos analógicos (circularidade e drift).
- **A placa:** a tela OLED espelhada ao vivo, um painel de diagnóstico (temperatura, clock, tráfego Bluetooth e USB, o pipeline de áudio e quanto tempo cada parte do firmware leva), configurações de energia, tela e USB, e **atualização de firmware com um clique** (sem botão BOOTSEL, sem copiar para drive).
- **Jogos:** sua biblioteca da Steam, Heroic e Lutris (Windows: Steam, GOG Galaxy e Epic) em lista e em grade de capas, com perfis de controle por jogo (cada configuração do controle que o jogo altera, incluindo um layout de botões e curvas dos analógicos, editados ao vivo na visão do controle), suporte ao DualSense por jogo e, no Linux, correções de configuração e a build Proton para DualSense.
- **Do seu jeito:** um tema nas cores do seu controle, Vidro, controles deslizantes de aparência, modos de visão de cores, Reduzir movimento, um narrador pelo alto-falante do controle, 7 idiomas, um ícone na bandeja.

O app conversa com firmware **v0.8 ou mais novo** por uma pequena interface USB de configuração (a calibração e a intensidade da vibração por lado precisam da **v0.9**; o app oferece a atualização). Os jogos nunca a veem, então a placa continua parecendo exatamente um DualSense de verdade. Código do app: **[MarcelineVPQ/DS5Dongle-OLED-Config-Web](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Config-Web)**.

> **A ferramenta de configuração no navegador foi aposentada**: o app de desktop faz tudo o que ela fazia e mais. O endereço antigo, [marcelinevpq.github.io/DS5Dongle-OLED-Config-Web](https://marcelinevpq.github.io/DS5Dongle-OLED-Config-Web/), agora é a página de download do app. Para gravar o firmware em um Pico sem o app, segure BOOTSEL ao conectá-lo e copie o `.uf2` da versão para o drive que aparecer.

## 🔄 Atualizações e instalação do firmware, sem complicação

**O app se mantém atualizado sozinho.**

- **Assinadas e conferidas.** Toda atualização vem de um único feed assinado com a nossa chave Ed25519. O app ignora um feed sem assinatura válida, confere o tamanho e o SHA-256 de cada download antes de instalar e nunca instala sozinho uma versão mais antiga.
- **Dois canais:** **Estável** para todos, **Desenvolvimento** para os recursos mais novos. **Voltar ao estável** leva o app e o firmware de volta à versão estável mais recente.
- **Você escolhe:** com **Instalar atualizações automaticamente** (o app pergunta uma vez), as atualizações são baixadas e conferidas em segundo plano, e a nova versão abre na próxima vez que você abrir o app. Com **Procurar atualizações automaticamente** desligado, o app só fica online quando você aperta um botão de verificar.
- **Em cada sistema:** no Linux, o AppImage coloca a nova versão ao lado dele e o atalho do menu passa a apontar para ela. No Windows, o instalador por usuário roda sem direitos de administrador. No macOS, o app mostra a atualização com um link para a versão publicada.

**O firmware é instalado com um clique.**

- **Incluído:** cada versão do app traz o firmware com que foi compilada e testada, então uma placa nova pode ser preparada offline. Firmware mais novo chega pelo mesmo feed assinado.
- **Primeira configuração:** conecte o Pico segurando o BOOTSEL (um Pico novo não precisa do botão). O app o encontra e instala o firmware.
- **Atualizações:** sem botão e sem copiar para um drive. O app reinicia a placa em BOOTSEL sozinho, grava o firmware por USB e reconecta.
- **Conferido antes de gravar:** o SHA-256 do arquivo precisa bater com o feed, e ele precisa ser firmware para este chip. Um arquivo que falha não grava nada.
- **Conferido depois de gravar:** o app espera o novo firmware iniciar. Se a placa voltar em BOOTSEL, o app avisa e oferece **Instalar de novo**. Enquanto o firmware é gravado, o app não reinicia, não fecha e não se atualiza.
- **Sua configuração fica:** configurações, pareamentos, calibração e atribuição de botões ficam numa parte própria da memória flash. Cada compilação do firmware é verificada para não chegar a essa parte, e um teste do app confere que uma atualização grava só os setores do arquivo de firmware.

## 🔬 Como é construído e testado

Toda mudança passa por todas as verificações automáticas. Toda versão ganha mais algumas. A lista completa, com os arquivos por trás de cada verificação: [docs/QUALITY.md](docs/QUALITY.md) (em inglês).

**Toda mudança**

- **App:** mais de 1.800 testes (de unidade, de integração e de segurança) no Linux, Windows e macOS, além de checagem de tipos, ESLint e Prettier.
- **Firmware:** mais de 270 testes no PC da lógica do firmware com AddressSanitizer e UndefinedBehaviorSanitizer, e cinco compilações do firmware (Pico 2 W, Pico W, depuração e duas variantes). A compilação falha se o código crítico em tempo sair da RAM ou se a configuração da flash mudar.
- **Regras do app:** as configurações de segurança do Electron, nada de HTML inseguro, um único sistema de temas sem cores soltas, nada de código ou arquivos duplicados sem veredito.
- **Vereditos por escrito:** todo laço que espera ou roda num caminho crítico, todo caminho de erro, todo cache e todo recurso que o código pega (um timer, um ouvinte, um dispositivo) tem um comentário com o seu veredito. Um novo sem veredito faz a verificação falhar.
- **Varreduras de segurança:** Semgrep, Gitleaks e npm audit, também toda semana.

**Toda versão de desenvolvimento**

- **Verificador da interface:** uma cópia própria do app com uma placa simulada abre cada página nos 7 idiomas, em dois tamanhos de janela. Ele falha com texto cortado, contraste abaixo do WCAG AA ou um controle sem nome.
- **Orçamentos de recursos** medidos no Linux e numa máquina virtual com Windows 11.
- **Teste no Windows** do app instalado numa máquina virtual com Windows 11, e um **teste rápido no macOS** num Mac de verdade do GitHub Actions (instalado como um usuário faz, aberto e operado).

**Antes de uma versão estável**

- **A verificação completa da interface:** cada tela e painel, os 7 idiomas, os dois tamanhos, cada tema e estilo, cada combinação de cores dos dois modelos de controle.
- **A medição longa dos orçamentos,** com um teste de 30 minutos do crescimento da memória, no Linux e no Windows.
- **Verificações no hardware** com uma placa de verdade e um DualSense: visualização ao vivo, resposta tátil, barra de luz, atribuição de botões, slots, gatilhos, contadores de áudio, reinício e BOOTSEL, primeira configuração, o atualizador, jogos.
- **Uso no dia a dia:** a aprovação da própria desenvolvedora depois de usar a versão no dia a dia.

**Eficiência**

- **Orçamentos que barram a versão:** no máximo 10 quadros por segundo numa página parada, menos de 2 % de um núcleo de CPU visível e parado, menos de 0,5 % escondido na bandeja, no máximo 275 MB de memória e menos de 5 MB de crescimento em 30 minutos. Um orçamento estourado é corrigido, não aumentado. Os números mais recentes: [BUDGETS.md](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Config-Web/blob/master/docs/BUDGETS.md).
- **Renderização na GPU:** os efeitos são desenhados pela placa de vídeo, e nada é desenhado com a janela escondida.
- **Firmware:** o caminho do áudio roda a partir da RAM, e o app mostra quanto tempo cada parte do firmware leva.

**Segurança e confiança**

- **Feed assinado** (Ed25519) e downloads conferidos por SHA-256, como acima.
- **Downloads verificáveis:** cada arquivo de uma versão tem um atestado de compilação. `gh attestation verify FILE -R MarcelineVPQ/DS5Dongle-OLED-Edition` mostra que ele foi compilado pelo fluxo de publicação deste repositório a partir da sua tag.
- **App protegido:** janela isolada (sandbox), Content Security Policy estrita, cada mensagem da janela conferida pelo remetente e validada, acesso USB limitado à placa e ao carregador de inicialização do Pico. Versões exatas das dependências, escolhidas pelo menos 2 semanas depois do lançamento delas. A auditoria: [SECURITY_AUDIT.md](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Config-Web/blob/master/docs/SECURITY_AUDIT.md).
- **Um DualSense de verdade para os jogos:** o descritor USB da placa é idêntico byte a byte ao da Sony (289 bytes), e a interface do configurador é separada, então os jogos nunca a veem.

**Acessibilidade e idiomas**

- **7 idiomas, completos:** um teste falha se faltar um texto em algum idioma, e os recursos do controle usam os nomes oficiais da Sony em cada idioma.
- **Legível e com nome:** o verificador da interface confere o contraste e o nome dos controles em cada idioma; o texto precisa caber em cada tamanho.
- **Do seu jeito:** modos de visão de cores, Reduzir movimento e uso pelo teclado em cada menu suspenso.

---

## Visão geral

Este projeto faz o Raspberry Pi Pico2W funcionar como uma ponte Bluetooth para o controle DualSense, permitindo conexão sem fio com suporte aprimorado à resposta tátil.

## Recursos

**Ponte principal (do upstream):**

- Conectividade completa do DualSense via Pico2W
- Resposta tátil HD (vibração avançada)
- Ponte Bluetooth sem fio
- Ganho da resposta tátil ajustável pelo volume do microfone
- LED e comportamento de desconexão configuráveis

**Acréscimos da OLED Edition:**

- **Gatilhos adaptáveis nativos do DualSense em jogos de PC no Linux/Proton, pela placa.** Jogos com suporte nativo ao DualSense acionam os gatilhos adaptáveis do controle sem fio pelo Pico, **1:1 com um DualSense ligado por cabo**. Requer um Proton que traga a correção #9034 do `winebus.sys` do Wine (Wine 11 / Proton-GE atual) com o Steam Input desativado; **nenhuma opção de inicialização é necessária** (o Wine 11 ativa o caminho nativo hidraw por padrão). Funciona **tanto na Steam quanto no Heroic** (Epic/GOG). O firmware deixa o Pico idêntico byte a byte a um DS5 real, para que o jogo o aceite (detalhes completos no [CHANGELOG.md](./CHANGELOG.md)).
- Tela de status opcional Pico-OLED-1.3 com **11 telas** (status, slots, barra de luz, teste de gatilhos, inclinação do giroscópio, touch pad, diagnóstico, CPU/clock, RSSI, medidores VU, configurações)
- **Calibração no Pico (v0.9)**: deslocamento do centro do analógico, zona inativa e raio de inclinação total, curso dos gatilhos e zero do giroscópio, por slot de emparelhamento, aplicados ao que os jogos recebem (nunca gravados no controle). Configurada pela calibração guiada do app.
- **Intensidade da vibração por lado (v0.9)**: esquerda e direita separadas, tanto para a resposta tátil quanto para a vibração clássica.
- **Curvas de sensibilidade (v0.9.5)**: uma curva por analógico (Linear, Preciso, Constante, Rápido, Digital ou quatro pontos personalizados), aplicada no Pico após a calibração, então funciona em todos os jogos; o perfil de um jogo pode trocar a sua enquanto ele roda.
- **Suporte ao DualSense Edge**: o firmware distingue o Edge de um DualSense pelo relatório de firmware que ele envia ao conectar; o Botão Fn e os botões traseiros podem ser atribuídos como qualquer outro botão, e a tela Status da OLED os mostra. Os perfis personalizados dele aparecem e são salvos no app PlayStation Accessories da Sony (upstream awalol 8a2f576; o app pode precisar de alguns segundos depois que o controle conecta). No modo DS5, o app não oferece mais uma atualização de firmware que falha (upstream awalol ae83907).
- **Info do controle (v0.9.5)**: o app pode ler as versões do próprio firmware do controle, a tensão da bateria e os contadores de uso pelo Pico (somente leitura; o número de série do controle é apagado antes de o app vê-lo).
- **Tempos por etapa (v0.9.5)**: o Pico mede o tempo de cada parte do próprio trabalho (Bluetooth, USB, áudio, entrada, OLED, os quadros de alto-falante e microfone do core1) para o cartão Tempos do app.
- **Espelho da tela OLED (v0.9)**: o app mostra a tela do Pico pixel por pixel, com ou sem o módulo OLED.
- **Atribuições de botões**: reatribua qualquer um dos 16 controles digitais (botões de ação, botões de direção, L1/R1/L2/R2, cliques dos analógicos, o Botão Criar e o Botão de opções), e o Botão Fn e os botões traseiros do DualSense Edge, a qualquer outro. Fica guardado no Pico e é aplicado antes de o host ver o relatório, então funciona em **qualquer jogo e sistema operacional sem software no host**; a identidade (sem reatribuição) é o padrão. Edite visualmente na página **Atribuições de botões** do [app de configuração](#️-comece-aqui-baixe-o-app), ou sem interface via `scripts/remap_test.py`. Salvo em um setor próprio da flash, sobrevive à reinicialização.
- **Suspensão e ativação do PC (upstream awalol)**: o controle desliga depois que o PC está em suspensão há 3 s (ligado por padrão) e, opcionalmente, o Botão PS ativa o PC (desligado por padrão: sem controle conectado, o Pico aparece então como um pequeno teclado só para ativação, "DS5 Dongle Wake Keyboard", que o PC precisa permitir que o ative). Com um controle conectado, o Pico é o DualSense exatamente como antes. Ambos ficam no app e no menu Settings da OLED. Para ativar, o PC precisa manter a USB energizada durante a suspensão.
- **Intensidade do efeito gatilho, amplificação do alto-falante, travar volume, escolha de microfone e alto-falante, pino de status (configurações do upstream, protocolo 1.13)**: gatilhos adaptáveis mais fracos em 10 passos (a força do próprio jogo por padrão), o pré-ganho do amplificador do controle, jogos impedidos de mudar o volume do controle, o microfone como Auto / Embutido / Headset, o áudio do PC em Auto / Alto-falante / Headset / Desligado, e um GPIO que mostra a conexão do controle. A luz do botão de desativar o som acompanha o mudo do microfone, e os botões de mudo e o nível do microfone do PC chegam ao controle. Uma configuração nova faz polling a 500 Hz.
- **Emparelhamento persistente de até 4 controles em slots**: vincule até quatro DualSense, troque entre eles pela OLED; o slot 0 reconecta automaticamente ao ligar
- **Seletor de cor da barra de luz** com 4 slots de favoritos + predefinições de efeito respiração / arco-íris / transição
- **Menu de configurações persistente** no OLED: 25 configurações em seis grupos (resposta tátil e gatilhos, áudio, conexão, tela e luzes, suspensão do PC), mais Reset to defaults e Wipe all slots, confirmadas segurando o botão
- **Escada de economia de energia da OLED**: ciclo manual de brilho (pressão longa em KEY1), escurecimento profundo automático com um pequeno ponto pulsante após 2 min parado, tela totalmente apagada após 15 min parado. Acorda na hora com um botão, emparelhamento do controle ou entrada. Proteção real contra burn-in, não só um ajuste de contraste.
- **Reinicialização por software** sem desconectar a USB, segurando `PS + Mute` no DS5 (funciona sem tela) ou **KEY0 + KEY1 juntos por 1 s** no módulo OLED (substitui o antigo gesto de duplo clique em KEY0, fácil de disparar sem querer ao passar telas rápido)
- **Alto-falante e microfone do controle limpos**: o caminho de áudio do core1 (Opus, reamostrador, operações de memória) roda a partir da RAM, o áudio sai nos relatórios `0x39` compactados do upstream, e o microfone é decodificado no core1 e cadenciado pelo clock USB do host. Isso eliminou o antigo chiado do alto-falante e as travadas com o microfone ligado (veja o [CHANGELOG.md](./CHANGELOG.md)). **Crédito para [awalol](https://github.com/awalol/DS5Dongle)**: este é o trabalho de áudio do upstream, portado para cá.
- **Auditoria da ponte principal**: correção crítica de stack overflow no caminho de áudio (resolve o antigo "áudio engasgando"), reforço de segurança, watchdog, validação de tamanho nas fronteiras HID/L2CAP (veja o [CHANGELOG.md](./CHANGELOG.md))

## 🐧 Linux: gatilhos adaptáveis nativos (Proton)

Para um jogo acionar os **gatilhos adaptáveis do controle pela placa** no Linux, é preciso um Proton que traga a correção **#9034** do `winebus.sys` do Wine. As versões oficiais do **GE-Proton 11** já a incluem. A build disponibilizada aqui, **`GE-Proton11-7.2-DualSense`** (veja [a versão dela](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Edition/releases/tag/proton-GE-Proton11-7.2-DualSense)), é o `GE-Proton11-7` oficial mais uma correção extra no `winebus.sys`: o DualSense informa seu número de série como um MAC Bluetooth separado por dois-pontos, o que torna o caminho do dispositivo impossível de abrir no Windows, então jogos que usam a libScePad da Sony não alcançam os gatilhos. Esta build substitui esses caracteres para que o caminho abra. Com `PROTON_PREFER_SDL=1`, ela também deixa jogos só com XInput usarem o controle como um gamepad padrão, com vibração ([changelog](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Config-Web/blob/master/docs/PROTON_CHANGELOG.md)). Os resultados de gatilhos abaixo foram verificados na build anterior, GE-Proton11-7-DualSense, cujo caminho nativo não mudou.

> Por quê: o bug #9034 suprime o dispositivo de gamepad SDL quando existe um dispositivo hidraw com o mesmo VID/PID, então o controle é visto *em todo lugar menos no jogo*. A correção deixa o caminho nativo (capaz de acionar os gatilhos) e um gamepad funcionando coexistirem.

**Configuração única**

1. **Instale a build do Proton.** Baixe `GE-Proton11-7.2-DualSense.tar.gz` [da versão](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Edition/releases/tag/proton-GE-Proton11-7.2-DualSense) e extraia em:
   - **Steam:** `~/.steam/root/compatibilitytools.d/`
   - **Heroic:** `~/.config/heroic/tools/proton/`

   Depois **reinicie a Steam por completo** para ela detectar a nova ferramenta de compatibilidade.
2. **Desative o Steam Input** para que o jogo fale com o controle de forma nativa: Steam → **Propriedades** do jogo **→ Controle → Desativar Steam Input** (ou globalmente: Configurações → Controle → desligue o suporte a controles PlayStation).

**Por jogo**

3. **Force o Proton:** Steam → clique com o botão direito no jogo → **Propriedades → Compatibilidade → Forçar o uso de uma ferramenta de compatibilidade específica do Steam Play → `GE-Proton11-7.2-DualSense`**. (Heroic: defina a *versão do Wine* do jogo como ela.)

**Nenhuma opção de inicialização é necessária**: o Wine 11 ativa o caminho nativo hidraw por padrão, então o Pico é entregue ao jogo automaticamente. É só abrir: os gatilhos adaptáveis nativos funcionam pelo Pico, 1:1 com um DualSense com cabo (verificado sem flags em Cyberpunk 2077, Uncharted, Spider-Man Remastered, The Last of Us Part I, Avatar, Indiana Jones, Star Wars: Galactic Racer, e **tanto na Steam quanto no Heroic**). O jogo precisa ter **suporte nativo ao DualSense** (jogos só com XInput dão vibração, mas sem gatilhos adaptáveis; é um limite do motor do jogo, não da placa). A primeira abertura com um Proton novo faz a configuração de locale/prefixo e pode ficar "Não respondendo" por um minuto; é configuração, não travamento; deixe terminar.

**Deixe o áudio do controle no perfil padrão.** O GE-Proton 11 encaminha a resposta tátil do DualSense por conta própria e espera a placa de som no perfil padrão **HiFi**. Uma regra do WirePlumber/PulseAudio que força o DualSense para **pro-audio** (uma dica comum para builds antigas do Proton) fez o Avatar enviar gatilhos com força zero e perder a resposta tátil HD (verificado; outros jogos portados pela Sony provavelmente se comportam igual). Remova essa regra, ou confira com `pactl list cards` que o DualSense mostra `Active Profile: HiFi (...)`.

### Heroic (Epic / GOG): a mesma receita, dois requisitos a mais

Os gatilhos nativos funcionam igual no Heroic, mas launchers fora da Steam são sensíveis a duas coisas no host que roubam o controle em silêncio:

1. **Feche a Steam por completo**: uma Steam em segundo plano pega o DualSense dos jogos fora da Steam (ou desative Configurações → Controle → suporte a controles PlayStation).
2. **Nada de `PROTON_PREFER_SDL` global**: essa variável de ambiente força o caminho SDL/Xbox e *suprime* os gatilhos nativos. Use-a só por jogo, como alternativa deliberada de controle genérico.

**Referência de opções de inicialização**

| Variável | Valor | Finalidade |
|---|---|---|
| `PROTON_ENABLE_HIDRAW` | `0x054c/0x0ce6` | *Legada / opcional, **não é necessária** no Wine 11*, que ativa o caminho nativo hidraw por padrão. Inofensiva se definida. Só importa em Protons antigos em que o hidraw era opcional (mas esses também não têm a correção #9034, então os gatilhos não funcionam neles de qualquer jeito). |
| `PROTON_PREFER_SDL` | `1` | *Alternativa, só entrada.* Força o caminho de gamepad SDL: entrada e vibração funcionam, mas como controle genérico/Xbox (**sem gatilhos**). Use só quando um jogo não tem suporte nativo ao DualSense e `ENABLE_HIDRAW` não ajuda. **Não** combine com `ENABLE_HIDRAW` em jogos com gatilhos. Precisa de um Proton que a respeite: a **GE-Proton11-7.2-DualSense** respeita (a página Jogos do app a define para jogos só com XInput); a antiga GE-Proton11-7-DualSense a ignora. |
| `PROTON_LOG` | `+hid` | *Só diagnóstico* (não para jogar normalmente). Grava um rastro da enumeração HID: a Steam o coloca em `~/steam-<appid>.log`; o Heroic define `PROTON_LOG_DIR` como sua pasta pessoal, então ele vai para `~/steam-0.log`. Útil para confirmar que o jogo abriu o caminho HID nativo. |

> **Heroic x Steam:** os gatilhos nativos funcionam nos **dois**, e nenhum precisa de opção de inicialização no Wine 11. Steam: defina o runner + desative o Steam Input. Heroic: defina o runner, **feche a Steam por completo** (uma Steam em segundo plano rouba o controle dos jogos fora da Steam) e mantenha `PROTON_PREFER_SDL` desligada. Em qualquer caso o jogo precisa suportar o DualSense de forma nativa: jogos só com XInput (por exemplo Ghostrunner, Control) dão vibração, mas sem gatilhos adaptáveis, em qualquer sistema. Esses jogos precisam de `PROTON_PREFER_SDL=1` e de um Proton que a respeite, como a **GE-Proton11-7.2-DualSense**.

## Hardware

### Obrigatório

| Item | Observações | Preço aprox. |
|---|---|---|
| **Raspberry Pi Pico 2 W** | MCU RP2350 com Bluetooth/WiFi CYW43 na placa. [Página oficial do produto](https://www.raspberrypi.com/products/raspberry-pi-pico-2/) | ~US$ 7 |
| **Controle Sony DualSense** | Qualquer DualSense do PS5 (VID `054C:0CE6`) ou DualSense Edge (`054C:0DF2`). | — |
| **Cabo USB-C** | Liga o Pico 2 W ao PC host. | — |

### Opcional (muito recomendado)

| Item | Observações | Preço aprox. |
|---|---|---|
| **Waveshare Pico-OLED-1.3** | Módulo OLED SH1107 de 128×64 (SKU HIPI1798). Encaixa direto nos pinos do Pico 2 W. O firmware o aciona automaticamente quando presente e simplesmente não faz nada quando ausente. [Página do produto](https://www.waveshare.com/pico-oled-1.3.htm) · [Wiki](https://www.waveshare.com/wiki/Pico-OLED-1.3) | ~US$ 6 |
| **Dissipador pequeno** para o RP2350 | Não é necessário: o firmware roda o RP2350 nos 150 MHz nominais e na tensão padrão (veja [Desempenho / Clock](#desempenho--clock)). Inofensivo se você já tiver um. | US$ 1–3 |

### Onde comprar

Tanto o Pico 2 W quanto o Waveshare Pico-OLED-1.3 são fáceis de encontrar no mundo todo:

- **Adafruit**, **Pimoroni**, **The Pi Hut**, **DigiKey**, **Mouser**: grandes distribuidores de eletrônicos (EUA / UE)
- **A loja da própria Waveshare** para o módulo OLED
- Lojas regionais da **Amazon**: pesquise `Raspberry Pi Pico 2 W` e `Waveshare Pico-OLED-1.3` (ou o SKU `HIPI1798`)
- **AliExpress**: estoque original da Waveshare e do Pico, além de clones; confira a avaliação do vendedor

## Primeiros passos

### Gravando o firmware

O [app](#️-comece-aqui-baixe-o-app) faz isso por você. Para gravar à mão (ou para a build do Pico W, que é um artefato de CI):

1. Segure o botão BOOTSEL do Pico2W
2. Conecte o Pico2W ao computador por USB
3. O dispositivo aparece como um armazenamento USB
4. Arraste e solte o arquivo de firmware .uf2 (das [releases](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Edition/releases)) no dispositivo

### Emparelhando o controle

1. Coloque o controle DualSense em modo de emparelhamento Bluetooth
2. Espere o Pico2W detectar e conectar
3. Depois de conectado, o dispositivo aparece no sistema host

## Configuração

Há quatro jeitos de configurar o firmware:

**App de configuração (recomendado):** o [DS5 Dongle OLED Edition Configurator](#️-comece-aqui-baixe-o-app) para Linux, Windows e macOS (a página Jogos lista jogos da Steam, Heroic e Lutris no Linux e da Steam, GOG Galaxy e Epic no Windows, e a configuração do Proton é só para Linux). Ele encontra a placa sozinho, aplica as mudanças ao vivo e salva na flash do Pico com um clique. Requer firmware v0.8+.

**No dispositivo (com o módulo OLED):** use o menu **Settings** na tela (11ª tela). Botões de direção ▲▼ movem a seleção, ✕ abre um grupo e ○ volta, ▶◀ ajustam valores, △ salva na flash.

**CLI no terminal (qualquer sistema, qualquer navegador):** instale o hidapi e use `scripts/set_ds5.py`:

```bash
pip install hidapi
scripts/set_ds5.py                            # mostra a configuração atual
scripts/set_ds5.py --auto-haptics fallback    # muda um campo e grava na flash
scripts/set_ds5.py --speaker-volume -10 --haptics-gain 1.5
scripts/set_ds5.py --slot 2                   # troca o slot de emparelhamento ativo
scripts/set_ds5.py --version                  # versão do firmware
scripts/set_ds5.py --rssi                     # sinal do BT ao vivo, dB em relação à faixa ideal (0 = ideal)
scripts/set_ds5.py --help                     # lista completa de flags
```

O script fala com o firmware por relatórios de recurso USB HID `0xF6`/`0xF7`/`0xF8`/`0xF9`. Desde a v0.6.12 esses relatórios não são declarados no descritor HID (os jogos precisam dele idêntico byte a byte ao de um DualSense real), então o script só funciona no Linux; nos outros sistemas use o app de configuração. Portado de [loteran/DS5Dongle](https://github.com/loteran/DS5Dongle) e ampliado para o campo `current_slot` deste fork.

**Botões do controle DualSense (alternativa antiga, sem OLED, sem CLI):**

### Volume do microfone

Controla o multiplicador de ganho da resposta tátil. Faixa: `[1.0 – 2.0]`.

### Mudo do alto-falante

Desativa o indicador de conexão por LED. Vale depois que o controle reconecta.

### Mudo do microfone

Desativa o comportamento de desconexão silenciosa.

## Observações

O Pico só fica visível para o sistema depois que o controle está conectado

Alguns comportamentos dependem de ciclos de reconexão para valer

### Indicador de bateria fraca no LED

Quando o DualSense conectado informa bateria em 10% ou menos (e não está carregando), o LED do Pico passa de aceso fixo para piscar a 1 Hz, para você ver o aviso de relance. O LED volta a ficar aceso fixo assim que o controle é ligado ao carregador ou o nível informado sobe de novo. O pisca também acontece quando `disable_pico_led` está definido: o aviso é tratado como crítico e passa por cima da preferência de LED apagado; o LED volta ao estado desativado (apagado) quando a bateria se recupera ou o controle começa a carregar.

Para desativar na compilação, configure com `-DENABLE_BATT_LED=OFF`. O padrão é ON.

## Microfone do DualSense por Bluetooth

**O microfone embutido do DualSense funciona pelo emparelhamento Bluetooth da placa** (desde a v0.6.8). O controle transmite o microfone como áudio Opus; o Pico o decodifica e o apresenta ao host como o dispositivo de captura USB padrão do DualSense, então qualquer app (Discord, OBS, voz no jogo) pode usá-lo como um microfone normal.

Por muito tempo isso foi considerado impossível: versões anteriores deste fork o documentavam como uma limitação rígida do firmware da Sony (e o driver de kernel `hid-playstation` do Linux ainda não o suporta). Descobriu-se que dependia de um único bit de ativação no relatório de áudio de saída da placa. **Todo o crédito para [awalol](https://github.com/awalol/DS5Dongle) (upstream) por identificá-lo.** O registro corrigido da investigação está em [BLUETOOTH_AUDIO_NOTES.md](./BLUETOOTH_AUDIO_NOTES.md).

**Como usar:**

- **Ligado por padrão.** Emparelhe o controle e o microfone começa a transmitir em um ou dois segundos, sem precisar de áudio do jogo (o Pico mantém a transmissão viva sozinho).
- **Aumente o volume de captura no host**: ele começa baixo. No Linux, encontre a placa de som com `arecord -l` e depois, por exemplo, `amixer -c <placa do DualSense> sset 'Headset' 90%`. Confira a captura com `scripts/mic_diag.sh capture`.
- **Desligue para poupar a bateria do controle**: OLED **Settings → Audio → Built-in mic**, ou a chave **Microfone embutido** no [app de configuração](#️-comece-aqui-baixe-o-app). O microfone sempre ligado mantém o subsistema de áudio do DS5 acordado, o que gasta a bateria bem mais rápido; desative-o se você não usa voz.

**Ressalvas:**

- O áudio do microfone é **mono** (decodificado em mono, duplicado no endpoint de captura estéreo).
- Desligar no meio da sessão corta o envio ao host na hora, mas o controle continua transmitindo até a próxima reconexão (não há um comando de "parar" conhecido); conectar do zero com a chave desligada nunca o ativa.
- O contador `Mic in:` da tela **Diagnostics** da OLED marca ~100/s enquanto o microfone transmite: um jeito rápido de confirmar que está ativo.
- **Ocultação de perda de pacotes:** quadros de microfone perdidos (link BT fraco, distância, interferência) são ocultados com Opus PLC, para a voz continuar contínua em vez de estalar/cortar, com uma pequena latência de buffer de jitter (~30 ms). O contador `Mic PLC:` da tela Diag só sobe quando quadros estão sendo ocultados; na prática, um medidor ao vivo da qualidade do link.

## Problemas conhecidos

- A resposta tátil HD pode não funcionar em todo jogo no Linux + Steam; isso é do lado do jogo (alguns títulos só enviam o áudio da resposta tátil HD por APIs específicas do Windows). Testado funcionando em Spider-Man Remastered; não entregue em Ghost of Tsushima, com o mesmo firmware e o mesmo controle.
- **Portas USB 3.0 podem atrapalhar o emparelhamento**: o controle pode ficar preso com a barra de luz âmbar/amarela fixa e nunca conectar, enquanto a mesma placa funciona bem numa porta USB 2.0. Isso é interferência de RF, não um bug do firmware; veja [Portas USB 3.0 e interferência no Bluetooth](#portas-usb-30-e-interferência-no-bluetooth) abaixo. (Desde a v0.6.8 o firmware tenta de novo automaticamente uma conexão travada em vez de ficar parado, o que recupera muitos casos limítrofes, mas não todos.)
- **Windows: o app não consegue abrir a placa** em PCs onde um DualSense de verdade já foi ligado por USB (a placa usa os mesmos IDs USB, e o Windows lembrou "nenhum driver necessário" para eles). A partir do app 0.9.2, a tela de configuração oferece **Reparar driver USB**: um pedido de administrador e depois reconectar. O PowerShell completo que ele executa, linha por linha, está no [README do app](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Config-Web#repair-usb-driver-exactly-what-it-runs).

## Portas USB 3.0 e interferência no Bluetooth

Se a placa funciona numa porta e não em outra, **tente primeiro uma porta USB 2.0.** Portas USB 3.0 e (principalmente) extensões USB 3.0 emitem ruído de RF de banda larga centrado perto de 2,4 GHz, a mesma faixa que o rádio Bluetooth do Pico usa para falar com o controle. É um problema bem documentado na indústria (Intel, *"USB 3.0 Radio Frequency Interference Impact on 2.4 GHz Wireless Devices"*), não específico deste firmware. O ruído reduz a sensibilidade do receptor BT do Pico, então o controle pode começar a conectar (barra de luz âmbar), mas o link fica ruidoso demais para concluir e trava no amarelo.

Soluções, mais ou menos em ordem de eficácia:

1. **Ligue a placa numa porta USB 2.0** (muitas vezes a solução mais simples; muitas placas-mãe/gabinetes têm as duas).
2. **Use uma extensão USB 2.0 curta** para afastar a placa alguns centímetros das portas USB 3.0 / do gabinete de metal, melhorando a linha de visão até o controle. Evite especificamente extensões USB 3.0.
3. **Use um hub USB 2.0 com alimentação** ligado na porta USB 3.0: o hub reduz a velocidade do link e acrescenta distância.
4. **Prenda um anel de ferrite** no cabo, perto da placa.
5. **Mantenha o controle mais perto / em linha de visão** da placa durante o emparelhamento.

O firmware continua tentando sozinho uma conexão travada, então deixar a placa ligada por ~10–20 s depois que a barra de luz fica âmbar pode fazê-la se recuperar sem reconectar.

## Desempenho / Clock

**Você não precisa fazer nada aqui.** O Pico 2 W roda com as mesmas configurações do upstream awalol/DS5Dongle v0.7.0:

- **Clock: 150 MHz**, o clock nominal do RP2350 (o mesmo com que ele inicia)
- **Tensão do núcleo: 1,10 V**, o padrão. O firmware a define ao iniciar, porque o RP2350 mantém a tensão de um firmware anterior até ser desconectado.
- **Barramento do chip Bluetooth (CYW43 PIO SPI): divisor de clock 2**
- **Flash: 75 MHz** (divisor 2), configurada pelo próprio firmware ao iniciar (`PICO_EMBED_XIP_SETUP`). Deixar na configuração do bootrom fazia o core1 travar em algumas placas ([issue #15](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Edition/issues/15)).

Versões antigas faziam overclock para 320 MHz a 1,20 V, uma configuração herdada do upstream. O upstream passou para 150 MHz quando o caminho de áudio passou a rodar da RAM, e este fork também, agora que o caminho de áudio e o caminho crítico de Bluetooth/USB do core0 rodam da RAM. 1,20 V está acima do máximo de operação de 1,16 V do RP2350, e nenhum dissipador é necessário a 150 MHz.

Por uma versão, uma compilação a partir do código ainda pode usar o overclock antigo como alternativa: `-DDS5_CLOCK_KHZ=320000` (1,20 V, divisor do barramento Bluetooth 5, divisor da flash 4). A compilação recusa uma configuração de Pico 2 W sem a configuração embutida da flash ou com a flash acima de 100 MHz. A build do Pico W não muda (200 MHz).

## Instruções de compilação

Para compilar o projeto a partir do código:

1. Instale o Pico SDK **2.3.0**. A compilação usa `pico_sdk_import.cmake`. O CI e as versões oficiais compilam com o toolchain GNU oficial da ARM **15.2.Rel1** (`arm-none-eabi`); o `gcc-arm-none-eabi` de uma distribuição geralmente funciona, mas o 15.2.Rel1 gera o mesmo binário da versão oficial. As versões exatas (SDK, commit do TinyUSB, download do toolchain) estão em `tools/toolchain.env`. Defina `PICO_TOOLCHAIN_PATH` como a pasta do toolchain descompactado se ele não estiver no seu `PATH`.
2. **Fixe o TinyUSB no commit de `tools/toolchain.env`** dentro do Pico SDK (`$PICO_SDK_PATH/lib/tinyusb`): TinyUSB 0.21 mais correções posteriores, como o upstream usa. O Pico SDK 2.3.0 ainda traz a 0.18.0, que não tem a forma de 4 argumentos de `TUD_AUDIO_EP_SIZE` usada pelo `tusb_config.h` deste projeto, e a compilação recusa qualquer TinyUSB que não seja a 0.21:
   ```bash
   cd "$PICO_SDK_PATH/lib/tinyusb"
   git fetch --depth 1 origin 2d56dc533e45e4e91b15e93fdab5e22e964f328d
   git checkout --detach FETCH_HEAD
   ```
3. Configure e compile com CMake + Ninja (ou Make):
   ```bash
   cd /path/to/DS5Dongle
   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DPICO_SDK_PATH="$PICO_SDK_PATH"
   cmake --build build --target ds5-bridge
   ```
4. O UF2 fica em `build/ds5-bridge-oled.uf2`. Grave com BOOTSEL como de costume.

Flags de compilação que valem conhecer:

- `-DENABLE_BATT_LED=ON` (padrão): pisca o LED do Pico com bateria fraca no DS5.
- `-DENABLE_SERIAL=ON`: envia o printf para USB CDC para depuração (padrão OFF; libera a UART nas builds de produção).
- `-DPICO_W_BUILD=ON`: compila para o Pico W original (sem áudio, clock menor). O padrão é o Pico 2 W.

## Diagnóstico e ferramentas de depuração

Dois jeitos de investigar problemas da ponte: no dispositivo, pela tela Diagnostics da OLED, e no host, por `scripts/mic_diag.sh` (Linux). O caminho pelo host é mais rápido: sem trocar de tela, sem ciclo de gravação, e roda enquanto o controle está em uso.

```
# Retrato instantâneo: o Pico está na USB? O ALSA o enumerou? A transmissão
# de captura está ativa? Há um controle emparelhado agora?
scripts/mic_diag.sh status

# arecord de 3 segundos no endpoint IN do microfone: mostra pico / RMS / contagem
# de não zeros, para distinguir "transmissão em silêncio" de "transmissão com áudio".
scripts/mic_diag.sh capture 3

# Igual a `status`, mas em loop, imprimindo só quando o estado muda. Útil para
# pegar o segundo exato em que o emparelhamento termina ou o áudio abre / fecha.
scripts/mic_diag.sh watch

# Leitura ao vivo do relatório de recurso de fabricante 0xFD do firmware (via /dev/hidraw):
# contagens + taxas de entrada BT, últimos IDs diferentes de 0x31, prefixos de bytes E os
# contadores do fluxo de gatilhos (0x02 do host recebidos / com AllowTriggerFFB definido /
# encaminhados ao BT). O bt-trace dá um veredito ("o driver do host não está enviando
# os bits Allow dos gatilhos" x "encaminhado, mas o controle não acionou"), o que
# de outra forma exigiria um analisador de protocolo USB.
scripts/mic_diag.sh bt-trace
```

Escrito originalmente para investigar a pesquisa (pausada) do microfone BT do DS5 (veja [BLUETOOTH_AUDIO_NOTES.md](./BLUETOOTH_AUDIO_NOTES.md)). O relatório de recurso `0xFD` e o decodificador `bt-trace` agora também trazem os contadores de fluxo de gatilhos adicionados para a [issue #3](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Edition/issues/3) (falta de tensão nos gatilhos adaptáveis em alguns jogos).

## Módulo de tela OLED (opcional)

Se você encaixar um [Waveshare Pico-OLED-1.3](#hardware) nos pinos do Pico2W, o firmware o aciona automaticamente como uma tela de status ao vivo. Nenhuma configuração necessária: o firmware simplesmente não faz nada quando não há OLED.

<img src="./assets/oled/oled-status-live.gif" alt="A tela Status ao vivo na OLED" width="512">

A tela Status ao vivo em uma placa real, capturada pixel a pixel pelo espelho da OLED. Todas as imagens de telas abaixo também são capturas reais da placa.

### Tela de abertura (1,5 s ao ligar)

A versão do firmware centralizada numa tela vazia por 1,5 segundo, depois pula para a tela Status.

### Onze telas, alternadas com KEY0 no módulo

Ordem: **Status → Slots → Lightbar → Trigger Test → Gyro Tilt → Touchpad → Diagnostics → CPU/Clock → BT Signal → VU Meters → Settings →** volta ao início. **Pressão curta em KEY0 avança; pressão curta em KEY1 volta**, em *todas* as telas. As interações de cada tela (alternar a predefinição de gatilho, alternar o modo da barra de luz, navegar pelo cursor de Settings, trocar de slot) ficam nos **botões do controle DualSense**, nunca em KEY0/KEY1, então os dois botões físicos do módulo OLED sempre significam a mesma coisa.

Toda tela também desenha **`>`** na borda superior esquerda (ao lado de KEY0) e **`<`** na borda inferior esquerda (ao lado de KEY1), para os rótulos na tela ficarem fisicamente junto dos botões.

#### 1. Status

Estado da conexão, endereço BD do DualSense emparelhado, % de bateria com barra (`+` carregando / `*` completa / `!` erro), posições dos analógicos ao vivo (cada caixa de analógico pisca invertida, um ponto preto numa caixa branca, enquanto o **L3 / R3** está pressionado), botões de direção, botões de ação (△ ◯ ✕ □), L1/R1 e barras de preenchimento analógicas de L2/R2. O indicador de link e a bateria usam pequenos ícones em pixel. Durante a carga, uma estimativa `~Nm` do tempo até 100% aparece ao lado da bateria.

<img src="./assets/oled/oled_sc01.png" alt="Tela Status na OLED" width="512">

#### 2. Slots

Emparelhamento persistente de até 4 controles em slots. Navegue pelos controles salvos, troque o slot ativo ou apague um slot. `>` é o cursor, `*` marca o slot ativo no momento.

<img src="./assets/oled/oled_sc02.png" alt="Tela Slots na OLED" width="512">

- **Botões de direção ▲▼**: movem o cursor entre os slots 0–3
- **△**: troca para o slot do cursor (desconecta o atual, reconecta ao controle salvo no slot)
- **□ segurado 1,5 s**: apaga o slot do cursor (descarta o bd_addr + a chave de link do BTstack)
- O slot ativo é salvo; o Pico reconecta a ele na próxima inicialização

#### 3. Seletor de cor da barra de luz

Incline o controle em cada eixo para ajustar R / G / B; o firmware envia a cor resultante à barra de luz de verdade do DualSense a 10 Hz, então a barra de luz É a prévia visual (a OLED é monocromática).

<img src="./assets/oled/oled_sc03.png" alt="Seletor de cor da barra de luz na OLED" width="512">

- Pressione **△ ◯ ✕ □** no controle para **salvar** a cor atual no slot de favorito 0 / 1 / 2 / 3
- Pressione **R1** no controle para alternar o rótulo do modo: `[LIVE]` → `[FAV0]` → `[FAV1]` → `[FAV2]` → `[FAV3]` → efeitos (Breathing / Rainbow / Fade) → `[HOST]` → de volta a `[LIVE]`
- `[HOST]` (o padrão) entrega a barra de luz ao PC / jogo; o Pico não define cor. Qualquer outro modo mantém a cor do Pico, seja o que for que o jogo envie
- Favoritos padrão: vermelho, verde, azul, branco

#### 4. Teste de gatilhos

Pressione **△** no controle para alternar sete efeitos de gatilho adaptável aplicados a L2 e R2. Puxe cada gatilho para sentir o efeito.

<img src="./assets/oled/oled_sc04.png" alt="Tela Trigger Test na OLED" width="512">

Ordem: **Off → Feedback → Weapon → Vibration → Bow → Gallop → Machine Gun → Off …** Parâmetros dos efeitos empacotados em bits conforme a engenharia reversa do [dualsensectl](https://github.com/nowrep/dualsensectl), todos na força máxima.

#### 5. Inclinação do giroscópio

Valores X/Y/Z do acelerômetro ao vivo com uma caixa de mira de 40×40. O ponto acompanha a inclinação do controle em tempo real e **fica centralizado quando o controle está deitado**: ele usa a calibração de IMU de fábrica, individual de cada controle (lida do relatório de recurso `0x05`), então a posição de repouso e o ganho estão corretos em qualquer controle. Inclinar para esquerda/direita e para frente/trás move o ponto na direção correspondente.

<img src="./assets/oled/oled_sc05.png" alt="Tela Gyro Tilt na OLED" width="512">

#### 6. Touch Pad

Desenho ao vivo da superfície do touch pad. Pontos aparecem nas posições atuais dos dedos; a contagem se atualiza conforme os dedos tocam / saem.

<img src="./assets/oled/oled_sc06.png" alt="Tela Touchpad na OLED" width="512">

#### 7. Diagnóstico

Lista rolável de contadores ao vivo: tempo ligado, estado do BT, fluxo de gatilhos host → BT (`host02` / `trig` / `tx`), taxa de entrada BT 0x31, quadros de áudio USB/s, pacotes BT 0x32/s e, no fim, contadores da investigação pausada do microfone. Os botões de direção ▲/▼ do controle rolam; pequenos `^` / `v` na borda direita indicam "há mais acima/abaixo". Somente leitura, então sem cursor. Útil para confirmar que a ponte está movendo bytes sem precisar de um cabo UART.

Os mesmos contadores também são exportados no relatório de recurso HID `0xFD` para ferramentas no host; veja `scripts/mic_diag.sh bt-trace` abaixo.

<img src="./assets/oled/oled_sc07.png" alt="Tela Diagnostics na OLED" width="512">

#### 8. CPU / Clock

Sinais vitais do RP2350 ao vivo: clock do sistema configurado (`Set`) e realmente em uso (`Real`), medido contra a referência do cristal, tensão do núcleo lida de volta do regulador e temperatura do chip (média de 256 amostras + EMA lenta, para o valor acompanhar a temperatura real do chip e não o ruído do ADC).

<img src="./assets/oled/oled_sc08.png" alt="Tela de diagnóstico CPU / Clock na OLED" width="512">

A mesma telemetria também é exportada no relatório de recurso HID `0xFC` para ferramentas.

#### 9. Sinal BT

Intensidade do sinal Bluetooth do link ativo ao vivo, em dB em relação à faixa ideal (0 = ideal, mostrado como *in range*; mais negativo é mais fraco), com uma barra de −30 dB (vazia) a 0 dB (cheia) e as palavras do app (Excellent / Good / Fair / Weak, ou seja Excelente / Bom / Regular / Fraco). Antes da primeira leitura, ou quando uma leitura falha, mostra `Signal: --` e `Link: unknown`.

<img src="./assets/oled/oled_sc09.png" alt="Tela BT Signal na OLED" width="512">

#### 10. Medidores VU

Medidores de pico ao vivo para os caminhos de áudio do alto-falante e da resposta tátil. Úteis para conferir o roteamento de áudio sem o controle ligado a um host.

<img src="./assets/oled/oled_sc10.png" alt="Tela VU Meters na OLED" width="512">

#### 11. Configurações

Editor de configuração persistente. Ele abre num menu de seis grupos, cada um com quantas configurações tem: **Haptics/trigger**, **Audio**, **Connection**, **Screen & lights**, **PC sleep** e **Reset**. Botões de direção ▲▼ movem a seleção; ✕ (ou ▶) abre um grupo e ○ volta ao menu, onde você parou. Dentro de um grupo, ▶◀ ajustam o valor. △ salva na flash de qualquer lugar. A linha selecionada é uma barra acesa; um valor alterado e ainda não salvo aparece numa janela escura nessa barra, e o título mostra `(*)` até você salvar.

<img src="./assets/oled/oled_sc11.png" alt="Tela Settings na OLED" width="512">

- **Haptics/trigger:** `Vibration L` e `Vibration R` (intensidade da vibração por lado), `Trig. effect` (intensidade do efeito dos gatilhos: `game`, a força do próprio jogo, ou -1 … -10 mais fraco), `AutoHap` (o modo do Audio Auto Haptics, abaixo), `AutoHap gain` (0–200 % em passos de 10 %, padrão 100 %) e `Auto filter` (o corte do passa-baixa antes do seguidor de envelope, 80 / 160 / 250 / 400 Hz, padrão 160 Hz: mais baixo = mais subgrave).
- **Audio:** `Spk volume`, `Spk boost` (reforço do alto-falante, o pré-ganho do amplificador do controle: `game`, por conta do jogo, ou 1 … 7), `Lock volume` (os jogos não podem mudar o volume do alto-falante, do headset nem do microfone do controle, o mudo nem a luz dele; o volume e o mudo do próprio PC continuam valendo), `Audio out` (Auto / SPK / Hset / off: onde o áudio do PC toca; Auto = o headset quando há um ligado ao controle), `Built-in mic` (a chave que desliga o microfone), `Mic used` (Auto / Int / Hset), `Mic level` (`PC` segue o nível do microfone do PC, ou 1–64), `Echo cancel` e `Audio buffer` (32–128).
- **Connection:** `Controller` (DS5 / DSE / Auto), `Poll rate` (250Hz / 500Hz / RT), `Disconnect` (desconectar um controle parado) e `Idle time` (10–60 min).
- **Screen & lights:** `Screen dim` e `Screen off` (minutos; `off` = nunca), `Wake on input` (usar o controle acende a tela) e `Status LED` (o LED do Pico).
- **PC sleep:** `Turn off ctrl` (desligar o controle quando o PC está em suspensão há 3 s, ligado por padrão) e `PS wakes PC` (sem controle conectado, a placa aparece como um teclado só para acordar, "DS5 Dongle Wake Keyboard", para o botão PS poder acordar o PC; desligado por padrão).
- **Reset:** `Reset to defaults` (segure △ por 2 s; volta todas as configurações ao padrão e mantém o slot atual) e `Wipe all slots` (segure △ por 2 s; apaga os 4 controles emparelhados e todas as chaves de link do BTstack).

Modos do `AutoHap`: `Off` (padrão) usa a resposta tátil só do jogo. `Fallback` gera vibração derivada só quando o jogo não envia dados táteis nativos (ex.: Ghost of Tsushima no Linux); note que ele também vibra com música e áudio da área de trabalho, enquanto jogos que *enviam* resposta tátil nativa (Spider-Man Remastered) passam sem mudança. `Mix` soma a derivada à nativa, `Replace` ignora a nativa por completo.

### Referência dos botões

Os dois botões físicos do módulo OLED servem **só para navegar**:

| Botão | Ação |
|---|---|
| **KEY0** pressão curta | Próxima tela (avançar) |
| **KEY1** pressão curta | Tela anterior (voltar) |
| **KEY1** pressionado por mais de 1,5 s | Muda o brilho uma vez enquanto pressionado; soltar não volta |
| **KEY0 + KEY1** segurados juntos ≥ 1 s | `watchdog_reboot`: reinicialização por software sem desconectar a USB |

As mudanças de estado de cada tela (alternar a predefinição de gatilho, alternar o modo da barra de luz, navegar em Settings, trocar de slot, salvar cores nos slots de favoritos) acontecem todas nos **botões do controle DualSense**, nunca em KEY0 / KEY1, então os dois botões físicos sempre significam a mesma coisa em todas as telas. Veja a seção de cada tela acima para saber qual botão do controle faz o quê.

### Pinagem (layout padrão de HAT para Pico da Waveshare)

| Função | GPIO |
|---|---|
| MOSI | 11 |
| SCK  | 10 |
| CS   | 9  |
| DC   | 8  |
| RST  | 12 |
| KEY0 | 15 |
| KEY1 | 17 |

### Recuperação por reinicialização por software

Dois jeitos de reiniciar a placa sem desconectar a USB, úteis se o emparelhamento travar ou se você quiser um estado limpo:

- **KEY0 + KEY1 da OLED segurados juntos por ≥ 1 s** → `watchdog_reboot`. Substitui o antigo gesto de "duplo clique em KEY0" das versões anteriores, já que a navegação rápida para frente disparava o temporizador do duplo clique sem querer.
- **`PS + Mute` do DualSense segurados por 2 segundos** → `watchdog_reboot` (funciona com ou sem a OLED; alternativa sem tela).

## Agradecimentos

Alguns recursos e ideias de design deste fork vêm de outros forks do upstream, com crédito:

- **[zurce/DS5Dongle-OLED](https://github.com/zurce/DS5Dongle-OLED)**: ícones em pixel art no cabeçalho de status da OLED (abordagem visual), o padrão de UX "segure para restaurar de fábrica" usado no item "Reset to defaults" da tela Settings (segure △ por 2 s para confirmar) e o sistema de emparelhamento BT persistente em vários slots da nova tela Slots (4 controles vinculados, botões de direção para navegar, △ para trocar de slot, □ segurado para apagar um slot, além de "Wipe all slots" no menu Settings).
- **[loteran/DS5Dongle](https://github.com/loteran/DS5Dongle)**: redescoberta independente da regressão `3a31bd7` do upstream que quebrou a saída do alto-falante / da resposta tátil HD (commit `c7a8d3c`); a correção no nosso `src/audio.cpp` restaura o mesmo sub-relatório SetStateData. Também é a origem do DSP do Audio Auto Haptics (passa-baixa de 1 polo + seguidor de envelope, veja Settings → Haptics/trigger) e da correção "não sincronizar o volume UAC1 do lado USB com a configuração persistente".
- **[SundayMoments/DS5_Bridge](https://github.com/SundayMoments/DS5_Bridge)**: uma ponte Pico DualSense irmã. A lógica de aplicação da reatribuição de botões e o conjunto de controles digitais em `src/remap.{h,cpp}` foram portados dela. A arquitetura de áudio dela (codificação Opus no dispositivo x um app companheiro opcional que codifica no host) também foi a referência para diagnosticar nosso trabalho na cadência do alto-falante ("warble"). Obrigado pela base.
- **[awalol/ds5dongle-config-web](https://github.com/awalol/ds5dongle-config-web)**: base do nosso app de configuração, [MarcelineVPQ/DS5Dongle-OLED-Config-Web](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Config-Web) (primeiro um app web, o app de desktop desde a 0.8.0). O fork adaptou o layout Config_body de 13 bytes do upstream ao nosso (19 bytes na época, 51 hoje) e acrescentou interface para os nossos acréscimos (emparelhamento em vários slots, Auto Haptics).
- **[PS5 Button Icons and Controls](https://zacksly.itch.io/ps5-button-icons-and-controls)** de **Zacksly**: o contorno do controle DualSense e os ícones de botões da página “Atribuições de botões” do configurador (app para desktop e ferramenta web), sob licença [CC BY 3.0](https://creativecommons.org/licenses/by/3.0/) (recoloridos para `currentColor` e recortados para os temas).

## Roadmap
- Confira o [plano do DS5Dongle](https://github.com/users/awalol/projects/5)

## Comunidade
- Entre no servidor do Discord: [Servidor do Discord](https://discord.gg/hM4ntchGCa)
- Se você encontrou um bug, abra uma issue.

## Referências

- [rafaelvaloto/Pico_W-Dualsense](https://github.com/rafaelvaloto/Pico_W-Dualsense): inspiração do projeto
- [egormanga/SAxense](https://github.com/egormanga/SAxense): POC da resposta tátil por Bluetooth
- [https://controllers.fandom.com/wiki/Sony_DualSense](https://controllers.fandom.com/wiki/Sony_DualSense): documentação da estrutura dos relatórios de dados do DualSense
- [Paliverse/DualSenseX](https://github.com/Paliverse/DualSenseX): pacote do relatório do alto-falante

---

*Esta tradução segue o [README.md](README.md) em inglês, que é a referência; ela pode ficar desatualizada.*

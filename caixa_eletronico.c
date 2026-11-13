#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define ARQ_CONTAS "contas.dat"
#define ARQ_TRANSACOES "transacoes.dat"
#define ARQ_TEMP "contas.tmp"

typedef enum
{
    CONTA_ENCERRADA,
    CONTA_ATIVA
} SituacaoConta;

typedef struct
{
    long numero;
    char titular[100];
    char cpf[15];
    double saldo;
    SituacaoConta situacao;
} Conta;

typedef enum
{
    DEPOSITO,
    SAQUE,
    TRANSFERENCIA_ENVIADA,
    TRANSFERENCIA_RECEBIDA
} TipoTransacao;

typedef struct
{
    long conta;
    long contaRelacionada;
    TipoTransacao tipo;
    double valor;
} Transacao;

enum
{
    OK = 1,
    ERRO_ARQUIVO = 0,
    ERRO_INEXISTENTE = -1,
    ERRO_ENCERRADA = -2,
    ERRO_VALOR = -3,
    ERRO_SALDO = -4,
    ERRO_MESMA_CONTA = -5,
    ERRO_SALDO_NAO_ZERO = -6,
    ERRO_JA_ATIVA = -7
};

void cadastrarConta(void);
void consultarConta(void);
int depositar(long numeroConta, double valor);
int sacar(long numeroConta, double valor);
int transferir(long contaOrigem, long contaDestino, double valor);
void listarContas(void);
int encerrarConta(long numeroConta);
int reativarConta(long numeroConta);
int buscarConta(FILE *arquivo, long numeroConta, Conta *conta);
int atualizarConta(FILE *arquivo, const Conta *conta);
int registrarTransacao(long conta, long relacionada, TipoTransacao tipo, double valor);
void extrato(long numeroConta);
int removerContasEncerradas(void);

static void lerTexto(const char *prompt, char *destino, size_t tamanho)
{
    printf("%s", prompt);
    if (fgets(destino, (int)tamanho, stdin) == NULL)
    {
        destino[0] = '\0';
        return;
    }
    destino[strcspn(destino, "\n")] = '\0';
}

static long lerLong(const char *prompt, int *valido)
{
    char buf[64], *fim;
    long v;

    lerTexto(prompt, buf, sizeof(buf));
    v = strtol(buf, &fim, 10);
    *valido = (fim != buf && *fim == '\0');
    return v;
}

static double lerDouble(const char *prompt, int *valido)
{
    char buf[64], *fim;
    double v;
    char *p;

    lerTexto(prompt, buf, sizeof(buf));
    for (p = buf; *p; p++)
        if (*p == ',')
            *p = '.';
    v = strtod(buf, &fim);
    *valido = (fim != buf && *fim == '\0');
    return v;
}

static int cpfValido(const char *cpf)
{
    int digitos = 0;
    for (; *cpf; cpf++)
    {
        if (isdigit((unsigned char)*cpf))
            digitos++;
        else if (*cpf != '.' && *cpf != '-')
            return 0;
    }
    return digitos == 11;
}

static void mensagemErro(int codigo)
{
    switch (codigo)
    {
    case ERRO_ARQUIVO:
        printf("Erro ao acessar o arquivo de dados.\n");
        break;
    case ERRO_INEXISTENTE:
        printf("Conta inexistente.\n");
        break;
    case ERRO_ENCERRADA:
        printf("Operacao negada: conta encerrada.\n");
        break;
    case ERRO_VALOR:
        printf("Valor invalido (deve ser maior que zero).\n");
        break;
    case ERRO_SALDO:
        printf("Saldo insuficiente.\n");
        break;
    case ERRO_MESMA_CONTA:
        printf("Contas de origem e destino devem ser diferentes.\n");
        break;
    case ERRO_SALDO_NAO_ZERO:
        printf("A conta so pode ser encerrada com saldo zero.\n");
        break;
    case ERRO_JA_ATIVA:
        printf("A conta ja esta ativa.\n");
        break;
    default:
        printf("Erro desconhecido.\n");
    }
}

static const char *textoSituacao(SituacaoConta s)
{
    return s == CONTA_ATIVA ? "ATIVA" : "ENCERRADA";
}

static void exibirConta(const Conta *c)
{
    printf("Conta:    %ld\n", c->numero);
    printf("Titular:  %s\n", c->titular);
    printf("CPF:      %s\n", c->cpf);
    printf("Saldo:    R$ %.2f\n", c->saldo);
    printf("Situacao: %s\n", textoSituacao(c->situacao));
}

int buscarConta(FILE *arquivo, long numeroConta, Conta *conta)
{
    Conta tmp;

    rewind(arquivo);
    while (fread(&tmp, sizeof(Conta), 1, arquivo) == 1)
    {
        if (tmp.numero == numeroConta)
        {
            *conta = tmp;
            return 1;
        }
    }
    return 0;
}

int atualizarConta(FILE *arquivo, const Conta *conta)
{
    Conta tmp;

    if (!buscarConta(arquivo, conta->numero, &tmp))
        return 0;

    if (fseek(arquivo, -(long)sizeof(Conta), SEEK_CUR) != 0)
        return 0;
    if (fwrite(conta, sizeof(Conta), 1, arquivo) != 1)
        return 0;

    return fflush(arquivo) == 0;
}

int registrarTransacao(long conta, long relacionada, TipoTransacao tipo, double valor)
{
    FILE *f = fopen(ARQ_TRANSACOES, "ab");
    Transacao t;
    int ok;

    if (f == NULL)
        return 0;

    t.conta = conta;
    t.contaRelacionada = relacionada;
    t.tipo = tipo;
    t.valor = valor;

    ok = (fwrite(&t, sizeof(Transacao), 1, f) == 1);
    fclose(f);
    return ok;
}

static int depositarInterno(long numeroConta, double valor,
                            TipoTransacao tipo, long relacionada, int registrar)
{
    FILE *f;
    Conta c;

    if (valor <= 0)
        return ERRO_VALOR;

    f = fopen(ARQ_CONTAS, "rb+");
    if (f == NULL)
        return ERRO_ARQUIVO;

    if (!buscarConta(f, numeroConta, &c))
    {
        fclose(f);
        return ERRO_INEXISTENTE;
    }
    if (c.situacao != CONTA_ATIVA)
    {
        fclose(f);
        return ERRO_ENCERRADA;
    }

    c.saldo += valor;
    if (!atualizarConta(f, &c))
    {
        fclose(f);
        return ERRO_ARQUIVO;
    }
    fclose(f);

    if (registrar)
        registrarTransacao(numeroConta, relacionada, tipo, valor);
    return OK;
}

static int sacarInterno(long numeroConta, double valor,
                        TipoTransacao tipo, long relacionada, int registrar)
{
    FILE *f;
    Conta c;

    if (valor <= 0)
        return ERRO_VALOR;

    f = fopen(ARQ_CONTAS, "rb+");
    if (f == NULL)
        return ERRO_ARQUIVO;

    if (!buscarConta(f, numeroConta, &c))
    {
        fclose(f);
        return ERRO_INEXISTENTE;
    }
    if (c.situacao != CONTA_ATIVA)
    {
        fclose(f);
        return ERRO_ENCERRADA;
    }
    if (c.saldo < valor)
    {
        fclose(f);
        return ERRO_SALDO;
    }

    c.saldo -= valor;
    if (!atualizarConta(f, &c))
    {
        fclose(f);
        return ERRO_ARQUIVO;
    }
    fclose(f);

    if (registrar)
        registrarTransacao(numeroConta, relacionada, tipo, valor);
    return OK;
}

int depositar(long numeroConta, double valor)
{
    return depositarInterno(numeroConta, valor, DEPOSITO, 0, 1);
}

int sacar(long numeroConta, double valor)
{
    return sacarInterno(numeroConta, valor, SAQUE, 0, 1);
}

int transferir(long contaOrigem, long contaDestino, double valor)
{
    FILE *f;
    Conta origem, destino;
    int r;

    if (contaOrigem == contaDestino)
        return ERRO_MESMA_CONTA;
    if (valor <= 0)
        return ERRO_VALOR;

    f = fopen(ARQ_CONTAS, "rb");
    if (f == NULL)
        return ERRO_ARQUIVO;

    if (!buscarConta(f, contaOrigem, &origem) ||
        !buscarConta(f, contaDestino, &destino))
    {
        fclose(f);
        return ERRO_INEXISTENTE;
    }
    fclose(f);

    if (origem.situacao != CONTA_ATIVA || destino.situacao != CONTA_ATIVA)
        return ERRO_ENCERRADA;
    if (origem.saldo < valor)
        return ERRO_SALDO;

    r = sacarInterno(contaOrigem, valor, TRANSFERENCIA_ENVIADA, contaDestino, 1);
    if (r != OK)
        return r;

    r = depositarInterno(contaDestino, valor, TRANSFERENCIA_RECEBIDA, contaOrigem, 1);
    if (r != OK)
    {

        depositarInterno(contaOrigem, valor, DEPOSITO, 0, 0);
        return r;
    }
    return OK;
}

int encerrarConta(long numeroConta)
{
    FILE *f = fopen(ARQ_CONTAS, "rb+");
    Conta c;

    if (f == NULL)
        return ERRO_ARQUIVO;

    if (!buscarConta(f, numeroConta, &c))
    {
        fclose(f);
        return ERRO_INEXISTENTE;
    }
    if (c.situacao == CONTA_ENCERRADA)
    {
        fclose(f);
        return ERRO_ENCERRADA;
    }
    if (c.saldo != 0.0)
    {
        fclose(f);
        return ERRO_SALDO_NAO_ZERO;
    }

    c.situacao = CONTA_ENCERRADA;
    if (!atualizarConta(f, &c))
    {
        fclose(f);
        return ERRO_ARQUIVO;
    }

    fclose(f);
    return OK;
}

int reativarConta(long numeroConta)
{
    FILE *f = fopen(ARQ_CONTAS, "rb+");
    Conta c;

    if (f == NULL)
        return ERRO_ARQUIVO;

    if (!buscarConta(f, numeroConta, &c))
    {
        fclose(f);
        return ERRO_INEXISTENTE;
    }
    if (c.situacao == CONTA_ATIVA)
    {
        fclose(f);
        return ERRO_JA_ATIVA;
    }

    c.situacao = CONTA_ATIVA;
    if (!atualizarConta(f, &c))
    {
        fclose(f);
        return ERRO_ARQUIVO;
    }

    fclose(f);
    return OK;
}

void cadastrarConta(void)
{
    FILE *f;
    Conta nova, existente;
    int valido;

    nova.numero = lerLong("Numero da conta: ", &valido);
    if (!valido || nova.numero <= 0)
    {
        printf("Numero de conta invalido.\n");
        return;
    }

    f = fopen(ARQ_CONTAS, "ab");
    if (f == NULL)
    {
        printf("Erro ao abrir %s.\n", ARQ_CONTAS);
        return;
    }
    fclose(f);

    f = fopen(ARQ_CONTAS, "rb+");
    if (f == NULL)
    {
        printf("Erro ao abrir %s.\n", ARQ_CONTAS);
        return;
    }

    if (buscarConta(f, nova.numero, &existente))
    {
        printf("Ja existe uma conta com o numero %ld.\n", nova.numero);
        fclose(f);
        return;
    }

    lerTexto("Nome do titular: ", nova.titular, sizeof(nova.titular));
    if (nova.titular[0] == '\0')
    {
        printf("Nome nao pode ser vazio.\n");
        fclose(f);
        return;
    }

    lerTexto("CPF (11 digitos): ", nova.cpf, sizeof(nova.cpf));
    if (!cpfValido(nova.cpf))
    {
        printf("CPF invalido.\n");
        fclose(f);
        return;
    }

    nova.saldo = 0.0;
    nova.situacao = CONTA_ATIVA;

    fseek(f, 0, SEEK_END);
    if (fwrite(&nova, sizeof(Conta), 1, f) != 1)
        printf("Erro ao gravar a conta.\n");
    else
        printf("Conta %ld cadastrada com sucesso!\n", nova.numero);

    fclose(f);
}

void consultarConta(void)
{
    FILE *f;
    Conta c;
    int valido;
    long numero = lerLong("Numero da conta: ", &valido);

    if (!valido)
    {
        printf("Numero invalido.\n");
        return;
    }

    f = fopen(ARQ_CONTAS, "rb");
    if (f == NULL)
    {
        printf("Nenhuma conta cadastrada.\n");
        return;
    }

    if (buscarConta(f, numero, &c))
        exibirConta(&c);
    else
        printf("Conta nao encontrada.\n");

    fclose(f);
}

void listarContas(void)
{
    FILE *f;
    Conta c;
    int valido, total = 0;
    long filtro;

    printf("1 - Todas | 2 - Somente ativas | 3 - Somente encerradas\n");
    filtro = lerLong("Filtro: ", &valido);
    if (!valido || filtro < 1 || filtro > 3)
        filtro = 1;

    f = fopen(ARQ_CONTAS, "rb");
    if (f == NULL)
    {
        printf("Nenhuma conta cadastrada.\n");
        return;
    }

    printf("\n%-10s %-30s %-15s %12s  %s\n",
           "CONTA", "TITULAR", "CPF", "SALDO", "SITUACAO");
    printf("------------------------------------------------------------------------------------\n");

    while (fread(&c, sizeof(Conta), 1, f) == 1)
    {
        if ((filtro == 2 && c.situacao != CONTA_ATIVA) ||
            (filtro == 3 && c.situacao != CONTA_ENCERRADA))
            continue;

        printf("%-10ld %-30.30s %-15s %12.2f  %s\n",
               c.numero, c.titular, c.cpf, c.saldo, textoSituacao(c.situacao));
        total++;
    }

    if (total == 0)
        printf("Nenhuma conta encontrada.\n");
    fclose(f);
}

void extrato(long numeroConta)
{
    FILE *f = fopen(ARQ_TRANSACOES, "rb");
    Transacao t;
    int total = 0;

    if (f == NULL)
    {
        printf("Nenhuma transacao registrada.\n");
        return;
    }

    printf("\nExtrato da conta %ld\n", numeroConta);
    printf("%-24s %-10s %12s\n", "TIPO", "RELAC.", "VALOR");
    printf("------------------------------------------------\n");

    while (fread(&t, sizeof(Transacao), 1, f) == 1)
    {
        if (t.conta != numeroConta)
            continue;

        switch (t.tipo)
        {
        case DEPOSITO:
            printf("%-24s", "Deposito");
            break;
        case SAQUE:
            printf("%-24s", "Saque");
            break;
        case TRANSFERENCIA_ENVIADA:
            printf("%-24s", "Transferencia enviada");
            break;
        case TRANSFERENCIA_RECEBIDA:
            printf("%-24s", "Transferencia recebida");
            break;
        }

        if (t.contaRelacionada != 0)
            printf(" %-10ld", t.contaRelacionada);
        else
            printf(" %-10s", "-");

        printf(" %12.2f\n", t.valor);
        total++;
    }

    if (total == 0)
        printf("Nenhuma transacao para esta conta.\n");
    fclose(f);
}

int removerContasEncerradas(void)
{
    FILE *orig = fopen(ARQ_CONTAS, "rb");
    FILE *temp;
    Conta c;
    int removidas = 0;

    if (orig == NULL)
        return -1;

    temp = fopen(ARQ_TEMP, "wb");
    if (temp == NULL)
    {
        fclose(orig);
        return -1;
    }

    while (fread(&c, sizeof(Conta), 1, orig) == 1)
    {
        if (c.situacao == CONTA_ATIVA)
        {
            if (fwrite(&c, sizeof(Conta), 1, temp) != 1)
            {
                fclose(orig);
                fclose(temp);
                remove(ARQ_TEMP);
                return -1;
            }
        }
        else
        {
            removidas++;
        }
    }

    fclose(orig);
    fclose(temp);

    if (remove(ARQ_CONTAS) != 0)
    {
        remove(ARQ_TEMP);
        return -1;
    }
    if (rename(ARQ_TEMP, ARQ_CONTAS) != 0)
        return -1;

    return removidas;
}

static void menuDeposito(void)
{
    int v1, v2, r;
    long conta = lerLong("Numero da conta: ", &v1);
    double valor = lerDouble("Valor do deposito: R$ ", &v2);

    if (!v1 || !v2)
    {
        printf("Entrada invalida.\n");
        return;
    }

    r = depositar(conta, valor);
    if (r == OK)
        printf("Deposito realizado com sucesso.\n");
    else
        mensagemErro(r);
}

static void menuSaque(void)
{
    int v1, v2, r;
    long conta = lerLong("Numero da conta: ", &v1);
    double valor = lerDouble("Valor do saque: R$ ", &v2);

    if (!v1 || !v2)
    {
        printf("Entrada invalida.\n");
        return;
    }

    r = sacar(conta, valor);
    if (r == OK)
        printf("Saque realizado com sucesso.\n");
    else
        mensagemErro(r);
}

static void menuTransferencia(void)
{
    int v1, v2, v3, r;
    long origem = lerLong("Conta de origem: ", &v1);
    long destino = lerLong("Conta de destino: ", &v2);
    double valor = lerDouble("Valor da transferencia: R$ ", &v3);

    if (!v1 || !v2 || !v3)
    {
        printf("Entrada invalida.\n");
        return;
    }

    r = transferir(origem, destino, valor);
    if (r == OK)
        printf("Transferencia realizada com sucesso.\n");
    else
        mensagemErro(r);
}

static void menuEncerrar(void)
{
    int v, r;
    long conta = lerLong("Numero da conta a encerrar: ", &v);

    if (!v)
    {
        printf("Numero invalido.\n");
        return;
    }

    r = encerrarConta(conta);
    if (r == OK)
        printf("Conta encerrada com sucesso.\n");
    else
        mensagemErro(r);
}

static void menuReativar(void)
{
    int v, r;
    long conta = lerLong("Numero da conta a reativar: ", &v);

    if (!v)
    {
        printf("Numero invalido.\n");
        return;
    }

    r = reativarConta(conta);
    if (r == OK)
        printf("Conta reativada com sucesso.\n");
    else
        mensagemErro(r);
}

static void menuExtrato(void)
{
    int v;
    long conta = lerLong("Numero da conta: ", &v);

    if (!v)
    {
        printf("Numero invalido.\n");
        return;
    }
    extrato(conta);
}

static void menuExclusaoFisica(void)
{
    char resp[8];
    int n;

    lerTexto("Isso remove DEFINITIVAMENTE todas as contas encerradas. Confirmar (S/N)? ",
             resp, sizeof(resp));
    if (toupper((unsigned char)resp[0]) != 'S')
    {
        printf("Operacao cancelada.\n");
        return;
    }

    n = removerContasEncerradas();
    if (n < 0)
        printf("Erro durante a exclusao fisica.\n");
    else
        printf("%d conta(s) encerrada(s) removida(s) do arquivo.\n", n);
}

int main(void)
{
    int opcao, valido;

    do
    {
        printf("\n===== CAIXA ELETRONICO =====\n");
        printf("1 - Cadastrar conta\n");
        printf("2 - Consultar conta\n");
        printf("3 - Realizar deposito\n");
        printf("4 - Realizar saque\n");
        printf("5 - Realizar transferencia\n");
        printf("6 - Listar contas\n");
        printf("7 - Encerrar conta\n");
        printf("8 - Extrato da conta\n");
        printf("9 - Reativar conta\n");
        printf("10 - Exclusao fisica de contas encerradas (admin)\n");
        printf("0 - Sair\n");

        opcao = (int)lerLong("Opcao: ", &valido);
        if (!valido)
            opcao = -1;
        printf("\n");

        switch (opcao)
        {
        case 1:
            cadastrarConta();
            break;
        case 2:
            consultarConta();
            break;
        case 3:
            menuDeposito();
            break;
        case 4:
            menuSaque();
            break;
        case 5:
            menuTransferencia();
            break;
        case 6:
            listarContas();
            break;
        case 7:
            menuEncerrar();
            break;
        case 8:
            menuExtrato();
            break;
        case 9:
            menuReativar();
            break;
        case 10:
            menuExclusaoFisica();
            break;
        case 0:
            printf("Encerrando o programa...\n");
            break;
        default:
            printf("Opcao invalida.\n");
        }
    } while (opcao != 0);

    return 0;
}

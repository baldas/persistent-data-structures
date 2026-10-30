import json
import re
from itertools import groupby
import pandas as pd
import dataframe_image as dfi

def formatar_vetor(lista):
    if not isinstance(lista, list):
        return str(lista)
    
    if not lista:
        return "[]"

    resultado = []
    
    # Agrupa elementos consecutivos testando se o valor é '**'
    for e_asterisco, grupo in groupby(lista, key=lambda x: str(x) == '**'):
        itens = list(grupo)
        if e_asterisco:
            qtd = len(itens)
            if qtd > 1:
                resultado.append(f"** x{qtd}")
            else:
                resultado.append("**")
        else:
            for item in itens:
                resultado.append(str(item).replace("'", ""))

    return "[" + ", ".join(resultado) + "]"


colunas = ["teste 1", "teste 2", "teste 3", "teste 4"]
linhas = ["arvore_persistente", "arvore_volatil", "hash_persistente", "hash_volatil"]
df = pd.DataFrame(index=linhas, columns=colunas)

with open('consolidado.json', 'r', encoding='utf-8') as f:
    dados = json.load(f)

for caminho, conteudo in dados.items():

    numeros_int = [int(n) for n in re.findall(r'\d+', caminho)]

    prefixo = caminho.split('/')[0].split('_')[0]
    sufixo = "persistente" if numeros_int[0] == 0 else "volatil"
    tipo_arquivo = f"{prefixo}_{sufixo}"
    tipo_teste = f"teste {numeros_int[1]+1}"

    carga = numeros_int[2]
    repetição = numeros_int[3]

    if isinstance(conteudo, dict):
        tem_dados = any(re.search(r'\d', str(item)) for item in conteudo["content"])

        if carga == 2:
            df.loc[tipo_arquivo, tipo_teste] = formatar_vetor(conteudo["content"])

df_estilizado = (
    df.style
    .format(precision=2)                             # Formata números com 2 casas decimais
    .set_table_styles([
        # Estilo do Cabeçalho
        {'selector': 'th', 'props': [
            ('background-color', '#1f2937'),
            ('color', 'white'),
            ('font-weight', 'bold'),
            ('text-align', 'center'),
            ('padding', '10px')
        ]},
        # Estilo das Células
        {'selector': 'td', 'props': [
            ('text-align', 'center'),
            ('padding', '8px'),
            ('font-family', 'sans-serif')
        ]},
        # Estilo do Título
        {'selector': 'caption', 'props': [
            ('caption-side', 'top'),
            ('font-size', '16px'),
            ('font-weight', 'bold'),
            ('margin-bottom', '10px')
        ]}
    ])
)

dfi.export(df_estilizado, "structures_vs_tests.png")
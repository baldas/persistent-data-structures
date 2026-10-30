import json
import re

import pandas as pd
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

        df.loc[tipo_arquivo, tipo_teste] = conteudo["content"]

df_estilizado = (
    df.style
    .format(precision=2)                             # Formata números com 2 casas decimais
    .set_caption("Tabela de Conteúdo das Estruturas (3 Cargas)")  # Título
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


import dataframe_image as dfi
dfi.export(df_estilizado, "tabela_final.png")
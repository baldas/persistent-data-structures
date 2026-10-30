import glob
import json
import re

# Função que separa o texto em pedaços de texto e inteiros
def chave_ordenacao_natural(texto):
    return [int(c) if c.isdigit() else c.lower() for c in re.split(r'(\d+)', texto)]

resultado = {}

# Aplica a chave no sorted dos arquivos
for caminho in sorted(glob.glob('../**/*.json', recursive=True), key=chave_ordenacao_natural):
    if 'consolidado' in caminho or 'tabela' in caminho:
        continue
        
    try:
        with open(caminho, 'r', encoding='utf-8') as f:
            conteudo = f.read()
            try:
                dados = json.loads(conteudo)
                
                # Se for dicionário de tarefas, ordena as chaves internas naturalmente também
                if isinstance(dados, dict):
                    chaves_ordenadas = sorted(dados.keys(), key=chave_ordenacao_natural)
                    resultado[caminho] = {k: dados[k] for k in chaves_ordenadas}
                else:
                    resultado[caminho] = dados
                    
            except:
                resultado[caminho] = conteudo
    except Exception as e:
        resultado[caminho] = f'Erro de leitura: {str(e)}'

with open('consolidado.json', 'w', encoding='utf-8') as out:
    json.dump(resultado, out, indent=2, ensure_ascii=False)

print('Arquivo consolidado.json gerado com sucesso com ordenação natural!')
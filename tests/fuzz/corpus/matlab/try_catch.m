try
  error('MPF:Fuzz', 'boom')
catch ME
  disp(ME.identifier)
end

pwd
cd /tmp
pwd

which ls cat which

echo first_line > /tmp/mysh_batch_test.txt
echo second_line >> /tmp/mysh_batch_test.txt
cat < /tmp/mysh_batch_test.txt | wc -l
rm -f /tmp/mysh_batch_test.txt

exit | echo "completed pipeline before exiting"
echo "this line should not execute"
